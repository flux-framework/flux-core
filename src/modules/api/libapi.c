/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/zfd.h"
#include "src/common/libutil/zconnect.h"

/* deferred message queue */
typedef struct {
    void *zs_resp[2]; /* [0]=read, [1]=write */
    void *zs_event[2];
} dq_t;


#define CMB_CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int rank;
    dq_t *dq;
    flux_t h;
    zloop_t *zloop;
    zctx_t *zctx;
    bool reactor_stop;
    int reactor_rc;
} cmb_t;

static void cmb_reactor_stop (void *impl, int rc);

#define ZLOOP_RETURN(c) \
    return ((c)->reactor_stop ? (-1) : (0))

static const struct flux_handle_ops cmb_ops;

static int cmb_request_sendmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return zfd_send_typemask (c->fd, FLUX_MSGTYPE_REQUEST, zmsg);
}

static int dq_resp_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    cmb_t *c = arg;
    zmsg_t *z = zmsg_recv_nowait (item->socket);
    if (z) {
        if (flux_handle_event_msg (c->h, FLUX_MSGTYPE_RESPONSE, &z) < 0) {
            cmb_reactor_stop (c, -1);
            goto done;
        }
    }
done:
    ZLOOP_RETURN(c);
}

static int dq_event_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    cmb_t *c = arg;
    zmsg_t *z = zmsg_recv_nowait (item->socket);
    if (z) {
        if (flux_handle_event_msg (c->h, FLUX_MSGTYPE_EVENT, &z) < 0) {
            cmb_reactor_stop (c, -1);
            goto done;
        }
    }
done:
    ZLOOP_RETURN(c);
}

static dq_t *dq_create (cmb_t *c)
{
    char *resp_uri = xasprintf ("inproc://dq-resp-%p", c);
    char *event_uri = xasprintf ("inproc://dq-event-%p", c);
    zmq_pollitem_t zp = { .events = ZMQ_POLLIN, .fd = -1 };
    dq_t *dq = xzmalloc (sizeof (*dq));

    zbind (c->zctx, &dq->zs_resp[1], ZMQ_PAIR, resp_uri, -1);
    zconnect (c->zctx, &dq->zs_resp[0], ZMQ_PAIR, resp_uri, -1, NULL);
    zp.socket = dq->zs_resp[0];
    if (zloop_poller (c->zloop, &zp, dq_resp_cb, c) < 0)
        oom ();

    zbind (c->zctx, &dq->zs_event[1], ZMQ_PAIR, event_uri, -1);
    zconnect (c->zctx, &dq->zs_event[0], ZMQ_PAIR, event_uri, -1, NULL);
    zp.socket = dq->zs_event[0];
    if (zloop_poller (c->zloop, &zp, dq_event_cb, c) < 0)
        oom ();

    free (resp_uri);
    free (event_uri);
    return dq;
}

static void dq_destroy (dq_t *dq)
{
    /* N.B. zctx destroy takes care of PAIR sockets */
    free (dq);
}

static int dq_put (dq_t *dq, zmsg_t **zmsg, int typemask)
{
    int rc = 0;
    if ((typemask & FLUX_MSGTYPE_EVENT))
        rc = zmsg_send (zmsg, dq->zs_event[1]);
    else if ((typemask & FLUX_MSGTYPE_RESPONSE))
        rc = zmsg_send (zmsg, dq->zs_resp[1]);
    else
        zmsg_destroy (zmsg);
    if (rc == 0)
        *zmsg = NULL;
    return rc;
}

static bool dq_get (dq_t *dq, zmsg_t **zmsg, int typemask)
{
    zmsg_t *z = NULL;
    if ((typemask & FLUX_MSGTYPE_EVENT))
        z = zmsg_recv_nowait (dq->zs_event[0]);
    else if ((typemask & FLUX_MSGTYPE_RESPONSE))
        z = zmsg_recv_nowait (dq->zs_resp[0]);
    if (z)
        *zmsg = z;
    return (z != NULL);
}

static zmsg_t *cmb_response_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;
    int typemask;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!dq_get (c->dq, &z, FLUX_MSGTYPE_RESPONSE)) {
        while ((z = zfd_recv_typemask (c->fd, &typemask, nonblock))
                                        && !(typemask & FLUX_MSGTYPE_RESPONSE))
            if (dq_put (c->dq, &z, typemask) < 0)
                oom ();
    }
    return z;
}

static int cmb_response_putmsg (void *impl, zmsg_t **zmsg)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return dq_put (c->dq, zmsg, FLUX_MSGTYPE_RESPONSE);
}

static int cmb_event_subscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.event.subscribe.%s", s ? s: "");
}

static int cmb_event_unsubscribe (void *impl, const char *s)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    return flux_request_send (c->h, NULL, "api.event.unsubscribe.%s", s ? s: "");
}

static zmsg_t *cmb_event_recvmsg (void *impl, bool nonblock)
{
    cmb_t *c = impl;
    zmsg_t *z;
    int typemask;

    assert (c->magic == CMB_CTX_MAGIC);
    if (!dq_get (c->dq, &z, FLUX_MSGTYPE_EVENT)) {
        while ((z = zfd_recv_typemask (c->fd, &typemask, nonblock))
                                        && !(typemask & FLUX_MSGTYPE_EVENT))
            dq_put (c->dq, &z, typemask);
    }
    return z;
}

static int cmb_rank (void *impl)
{
    cmb_t *c = impl;
    assert (c->magic == CMB_CTX_MAGIC);
    if (c->rank == -1) {
        if (flux_info (c->h, &c->rank, NULL, NULL) < 0)
            return -1;
    }
    return c->rank;
}

static int cmb_reactor_start (void *impl)
{
    cmb_t *c = impl;
    c->reactor_stop = false;
    c->reactor_rc = 0;
    zloop_start (c->zloop);
    return c->reactor_rc;
}

static void cmb_reactor_stop (void *impl, int rc)
{
    cmb_t *c = impl;
    c->reactor_stop = true;
    c->reactor_rc = rc;
}

static int unix_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    cmb_t *c = arg;
    bool nonblock = false;
    int typemask;
    zmsg_t *z;

    if (item->revents & ZMQ_POLLIN) {
        if ((z = zfd_recv_typemask (c->fd, &typemask, nonblock))) {
            if (flux_handle_event_msg (c->h, typemask, &z) < 0) {
                cmb_reactor_stop (c, -1);
                goto done;
            }
        }
    }
    if (item->revents & ZMQ_POLLERR) {
        cmb_reactor_stop (c, -1);
        goto done;
    }
done:
    ZLOOP_RETURN(c);
}

static int fd_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    cmb_t *c = arg;
    if (flux_handle_event_fd (c->h, item->fd, item->revents) < 0)
        cmb_reactor_stop (c, -1);
    ZLOOP_RETURN(c);
}

static int cmb_reactor_fd_add (void *impl, int fd, short events)
{
    cmb_t *c = impl;
    zmq_pollitem_t item = { .fd = fd, .events = events };

#ifdef ZMQ_IGNERR
    item.events |= ZMQ_IGNERR;
#endif
    if (zloop_poller (c->zloop, &item, (zloop_fn *)fd_cb, c) < 0)
        return -1;
#ifndef ZMQ_IGNERR
    zloop_set_tolerant (c->zloop, &item);
#endif
    return 0;
}

static void cmb_reactor_fd_remove (void *impl, int fd, short events)
{
    cmb_t *c = impl;
    zmq_pollitem_t item = { .fd = fd, .events = events };

    zloop_poller_end (c->zloop, &item); /* FIXME: 'events' are ignored */
}

static int zs_cb (zloop_t *zl, zmq_pollitem_t *item, void *arg)
{
    cmb_t *c = arg;
    if (flux_handle_event_zs (c->h, item->socket, item->revents) < 0)
        cmb_reactor_stop (c, -1);
    ZLOOP_RETURN(c);
}

static int cmb_reactor_zs_add (void *impl, void *zs, short events)
{
    cmb_t *c = impl;
    zmq_pollitem_t item = { .socket = zs, .events = events };

    return zloop_poller (c->zloop, &item, (zloop_fn *)zs_cb, c);
}

static void cmb_reactor_zs_remove (void *impl, void *zs, short events)
{
    cmb_t *c = impl;
    zmq_pollitem_t item = { .socket = zs, .events = events };

    zloop_poller_end (c->zloop, &item); /* FIXME: 'events' are ignored */
}

static int tmout_cb (zloop_t *zl, int timer_id, void *arg)
{
    cmb_t *c = arg;

    if (flux_handle_event_tmout (c->h, timer_id) < 0)
        cmb_reactor_stop (c, -1);
    ZLOOP_RETURN(c);
}

static int cmb_reactor_tmout_add (void *impl, unsigned long msec, bool oneshot)
{
    cmb_t *c = impl;
    int times = oneshot ? 1 : 0;

    return zloop_timer (c->zloop, msec, times, tmout_cb, c);
}

static void cmb_reactor_tmout_remove (void *impl, int timer_id)
{
    cmb_t *c = impl;

    zloop_timer_end (c->zloop, timer_id);
}

static void cmb_fini (void *impl)
{
    cmb_t *c = impl;

    assert (c->magic == CMB_CTX_MAGIC);
    if (c->fd >= 0)
        (void)close (c->fd);
    dq_destroy (c->dq);
    if (c->zctx)
        zctx_destroy (&c->zctx); /* destroys all sockets created in zctx */
    if (c->zloop)
        zloop_destroy (&c->zloop);
    free (c);
}

static bool pidcheck (const char *pidfile)
{
    pid_t pid;
    FILE *f = NULL;
    bool running = false;

    if (!(f = fopen (pidfile, "r")))
        goto done;
    if (fscanf (f, "%u", &pid) != 1 || kill (pid, 0) < 0)
        goto done;
    running = true;
done:
    if (f)
        (void)fclose (f);
    return running;
}

flux_t flux_api_openpath (const char *path, int flags)
{
    cmb_t *c = NULL;
    struct sockaddr_un addr;
    zmq_pollitem_t zp;
    char *cpy = xstrdup (path);
    char *pidfile = NULL;

    c = xzmalloc (sizeof (*c));
    c->magic = CMB_CTX_MAGIC;
    c->rank = -1;
    if (!(c->zctx = zctx_new ()))
        err_exit ("zctx_new");
    zctx_set_iothreads (c->zctx, 0);

    if (!(c->zloop = zloop_new ()))
        oom ();
    c->dq = dq_create (c);

    c->fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0)
        goto error;
    zp.socket = NULL;
    zp.fd = c->fd;
    zp.events = ZMQ_POLLIN | ZMQ_POLLERR;
    if (zloop_poller (c->zloop, &zp, unix_cb, c) < 0)
        oom ();

    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, path, sizeof (addr.sun_path) - 1);

    if (asprintf (&pidfile, "%s/cmbd.pid", dirname (cpy)) < 0)
        oom ();

    for (;;) {
        if (!pidcheck (pidfile))
            goto error;
        if (connect (c->fd, (struct sockaddr *)&addr,
                                   sizeof (struct sockaddr_un)) == 0)
            break;
        usleep (100*1000);
    }
    c->h = flux_handle_create (c, &cmb_ops, flags);
    return c->h;
error:
    if (c)
        cmb_fini (c);
    if (cpy)
        free (cpy);
    if (pidfile)
        free (pidfile);
    return NULL;
}

flux_t flux_api_open (void)
{
    const char *val;
    char path[PATH_MAX + 1];
    int flags = 0;

    if ((val = getenv ("FLUX_API_PATH"))) {
        if (strlen (val) > PATH_MAX) {
            err ("Crazy value for FLUX_API_PATH!");
            return (NULL);
        }
        snprintf (path, sizeof (path), "%s", val);
    } else {
        const char *tmpdir = getenv ("FLUX_TMPDIR");
        if (!tmpdir)
            tmpdir = getenv ("TMPDIR");
        if (!tmpdir)
            tmpdir = "/tmp";
        snprintf (path, sizeof (path), "%s/flux-api", tmpdir);
    }

    if ((val = getenv ("FLUX_TRACE_APISOCK")) && !strcmp (val, "1"))
        flags = FLUX_FLAGS_TRACE;

    return flux_api_openpath (path, flags);
}

void flux_api_close (flux_t h)
{
    flux_handle_destroy (&h);
}

static const struct flux_handle_ops cmb_ops = {
    .request_sendmsg = cmb_request_sendmsg,
    .response_recvmsg = cmb_response_recvmsg,
    .response_putmsg = cmb_response_putmsg,
    .event_recvmsg = cmb_event_recvmsg,
    .event_subscribe = cmb_event_subscribe,
    .event_unsubscribe = cmb_event_unsubscribe,
    .rank = cmb_rank,
    .reactor_stop = cmb_reactor_stop,
    .reactor_start = cmb_reactor_start,
    .reactor_fd_add = cmb_reactor_fd_add,
    .reactor_fd_remove = cmb_reactor_fd_remove,
    .reactor_zs_add = cmb_reactor_zs_add,
    .reactor_zs_remove = cmb_reactor_zs_remove,
    .reactor_tmout_add = cmb_reactor_tmout_add,
    .reactor_tmout_remove = cmb_reactor_tmout_remove,
    .impl_destroy = cmb_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
