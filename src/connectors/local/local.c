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

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libutil/ev_zlist.h"

#define CTX_MAGIC   0xf434aaab
typedef struct {
    int magic;
    int fd;
    int rank;
    flux_t h;
    zlist_t *putmsg;

    /* Event loop machinery.
     * main/putmsg pollers are turned off and on to give putmsg priority
     */
    struct ev_loop *loop;
    ev_io unix_w;
    ev_zlist putmsg_w;
    int loop_rc;

    flux_msg_f msg_cb;
    void *msg_cb_arg;

    zhash_t *watchers;
    int timer_seq;
} ctx_t;

#define HASHKEY_LEN 80

typedef struct {
    ev_timer w;
    FluxTmoutHandler cb;
    void *arg;
    int id;
} atimer_t;

typedef struct {
    ev_zmq w;
    FluxZsHandler cb;
    void *arg;
} azs_t;

typedef struct {
    ev_io w;
    FluxFdHandler cb;
    void *arg;
} afd_t;

static void op_reactor_stop (void *impl, int rc);
static void sync_msg_watchers  (ctx_t *c);


static const struct flux_handle_ops handle_ops;

static int op_sendmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    return zfd_send (c->fd, zmsg);
}

static zmsg_t *op_recvmsg_putmsg (ctx_t *c)
{
    zmsg_t *zmsg = zlist_pop (c->putmsg);
    if (zmsg) {
        if (zlist_size (c->putmsg) == 0)
            sync_msg_watchers (c);
    }
    return zmsg;
}

static zmsg_t *op_recvmsg (void *impl, bool nonblock)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    zmsg_t *zmsg = NULL;

    if (!(zmsg = op_recvmsg_putmsg (c)))
        zmsg = zfd_recv (c->fd, nonblock);
    return zmsg;
}

static int op_putmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int oldcount = zlist_size (c->putmsg);

    if (zlist_append (c->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (c);
    return 0;
}

static int op_pushmsg (void *impl, zmsg_t **zmsg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int oldcount = zlist_size (c->putmsg);

    if (zlist_push (c->putmsg, *zmsg) < 0)
        oom ();
    *zmsg = NULL;
    if (oldcount == 0)
        sync_msg_watchers (c);
    return 0;
}

static void op_purge (void *impl, flux_match_t match)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    zmsg_t *zmsg = zlist_first (c->putmsg);

    while (zmsg) {
        if (flux_msg_cmp (zmsg, match)) {
            zlist_remove (c->putmsg, zmsg);
            zmsg_destroy (&zmsg);
        }
        zmsg = zlist_next (c->putmsg);
    }
}

static int op_event_subscribe (void *impl, const char *s)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char *topic = xasprintf ("api.event.subscribe.%s", s ? s : "");
    int rc = flux_json_request (c->h, FLUX_NODEID_ANY,
                                      FLUX_MATCHTAG_NONE, topic, NULL);
    free (topic);
    return rc;
}

static int op_event_unsubscribe (void *impl, const char *s)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char *topic = xasprintf ("api.event.unsubscribe.%s", s ? s : "");
    int rc = flux_json_request (c->h, FLUX_NODEID_ANY,
                                      FLUX_MATCHTAG_NONE, topic, NULL);
    free (topic);
    return rc;
}

static int op_rank (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    if (c->rank == -1) {
        if (flux_info (c->h, &c->rank, NULL, NULL) < 0)
            return -1;
    }
    return c->rank;
}

static int op_reactor_start (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    c->loop_rc = 0;
    ev_run (c->loop, 0);
    return c->loop_rc;
}

static void op_reactor_stop (void *impl, int rc)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    c->loop_rc = rc;
    ev_break (c->loop, EVBREAK_ALL);
}

static void putmsg_cb (struct ev_loop *loop, ev_zlist *w, int revents)
{
    ctx_t *c = (ctx_t *)((char *)w - offsetof (ctx_t, putmsg_w));
    assert (c->magic == CTX_MAGIC);

    assert (zlist_size (c->putmsg) > 0);
    if (c->msg_cb) {
        if (c->msg_cb (c->h, c->msg_cb_arg) < 0)
            op_reactor_stop (c, -1);
    }
}

static void unix_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    ctx_t *c = (ctx_t *)((char *)w - offsetof (ctx_t, unix_w));
    assert (c->magic == CTX_MAGIC);

    if (revents & EV_ERROR) {
        op_reactor_stop (c, -1);
    } else if (revents & EV_READ) {
        assert (zlist_size (c->putmsg) == 0);
        if (c->msg_cb) {
            if (c->msg_cb (c->h, c->msg_cb_arg) < 0)
                op_reactor_stop (c, -1);
        }
    }
}

/* Enable/disable the appropriate watchers to give putmsg priority
 * over the main unix socket.
 */
static void sync_msg_watchers (ctx_t *c)
{
    if (c->msg_cb == NULL) {
        ev_io_stop (c->loop, &c->unix_w);
        ev_zlist_stop (c->loop, &c->putmsg_w);
    } else if (zlist_size (c->putmsg) > 0) {
        ev_io_stop (c->loop, &c->unix_w);
        ev_zlist_start (c->loop, &c->putmsg_w);
    } else {
        ev_zlist_stop (c->loop, &c->putmsg_w);
        ev_io_start (c->loop, &c->unix_w);
    }
}

static int op_reactor_msg_add (void *impl, flux_msg_f cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    int rc = -1;

    if (c->msg_cb != NULL) {
        errno = EBUSY;
        goto done;
    }
    c->msg_cb = cb;
    c->msg_cb_arg = arg;
    sync_msg_watchers (c);
    rc = 0;
done:
    return rc;
}

static void op_reactor_msg_remove (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    c->msg_cb = NULL;
    c->msg_cb_arg = NULL;
    sync_msg_watchers (c);
}

static void fd_cb (struct ev_loop *loop, ev_io *w, int revents)
{
    afd_t *f = (afd_t *)((char *)w - offsetof (afd_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    if (f->cb (c->h, w->fd, etoz (revents), f->arg) < 0)
        op_reactor_stop (c, -1);
}

static int op_reactor_fd_add (void *impl, int fd, int events,
                               FluxFdHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    afd_t *f = xzmalloc (sizeof (*f));
    ev_io_init (&f->w, fd_cb, fd, ztoe (events));
    f->w.data = c;
    f->cb = cb;
    f->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    zhash_update (c->watchers, hashkey, f);
    zhash_freefn (c->watchers, hashkey, free);

    ev_io_start (c->loop, &f->w);

    return 0;
}

static void op_reactor_fd_remove (void *impl, int fd, int events)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "fd:%d:%d", fd, events);
    afd_t *f = zhash_lookup (c->watchers, hashkey);
    if (f) {
        ev_io_stop (c->loop, &f->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void zs_cb (struct ev_loop *loop, ev_zmq *w, int revents)
{
    azs_t *z = (azs_t *)((char *)w - offsetof (azs_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    int rc = z->cb (c->h, w->zsock, etoz (revents), z->arg);
    if (rc < 0)
        op_reactor_stop (c, -1);
}

static int op_reactor_zs_add (void *impl, void *zs, int events,
                               FluxZsHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    azs_t *z = xzmalloc (sizeof (*z));
    ev_zmq_init (&z->w, zs_cb, zs, ztoe (events));
    z->w.data = c;
    z->cb = cb;
    z->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    zhash_update (c->watchers, hashkey, z);
    zhash_freefn (c->watchers, hashkey, free);

    ev_zmq_start (c->loop, &z->w);
    return 0;
}

static void op_reactor_zs_remove (void *impl, void *zs, int events)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "zsock:%p:%d", zs, events);
    azs_t *z = zhash_lookup (c->watchers, hashkey);
    if (z) {
        ev_zmq_stop (c->loop, &z->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void timer_cb (struct ev_loop *loop, ev_timer *w, int revents)
{
    atimer_t *t = (atimer_t *)((char *)w - offsetof (atimer_t, w));
    ctx_t *c = w->data;
    assert (c->magic == CTX_MAGIC);

    if (t->cb (c->h, t->arg) < 0)
         op_reactor_stop (c, -1);
}

static int op_reactor_tmout_add (void *impl, unsigned long msec, bool oneshot,
                                  FluxTmoutHandler cb, void *arg)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    double after = (double)msec / 1000.0;
    double repeat = oneshot ? 0 : after;
    char hashkey[HASHKEY_LEN];

    atimer_t *t = xzmalloc (sizeof (*t));
    t->id = c->timer_seq++;
    t->w.data = c;
    ev_timer_init (&t->w, timer_cb, after, repeat);
    t->cb = cb;
    t->arg = arg;

    snprintf (hashkey, sizeof (hashkey), "timer:%d", t->id);
    zhash_update (c->watchers, hashkey, t);
    zhash_freefn (c->watchers, hashkey, free);

    ev_timer_start (c->loop, &t->w);

    return t->id;
}

static void op_reactor_tmout_remove (void *impl, int timer_id)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    char hashkey[HASHKEY_LEN];

    snprintf (hashkey, sizeof (hashkey), "timer:%d", timer_id);
    atimer_t *t = zhash_lookup (c->watchers, hashkey);
    if (t) {
        ev_timer_stop (c->loop, &t->w);
        zhash_delete (c->watchers, hashkey);
    }
}

static void op_fini (void *impl)
{
    ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (c->fd >= 0)
        (void)close (c->fd);
    if (c->loop)
        ev_loop_destroy (c->loop);
    if (c->putmsg) {
        zmsg_t *zmsg;
        while ((zmsg = zlist_pop (c->putmsg)))
            zmsg_destroy (&zmsg);
        zlist_destroy (&c->putmsg);
    }
    zhash_destroy (&c->watchers);
    c->magic = ~CTX_MAGIC;
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

/* Path is interpreted as the directory containing the unix domain socket.
 * If NULL, flux_get_tmpdir() is used.
 */
flux_t connector_init (const char *path, int flags)
{
    ctx_t *c = NULL;
    struct sockaddr_un addr;
    char pidfile[PATH_MAX + 1];
    char sockfile[PATH_MAX + 1];
    int n;

    if (!path)
        path = flux_get_tmpdir ();

    n = snprintf (sockfile, sizeof (sockfile), "%s/flux-api", path);
    if (n >= sizeof (sockfile)) {
        errno = EINVAL;
        goto error;
    }
    n = snprintf (pidfile, sizeof (pidfile), "%s/broker.pid", path);
    if (n >= sizeof (pidfile)) {
        errno = EINVAL;
        goto error;
    }

    c = xzmalloc (sizeof (*c));
    c->magic = CTX_MAGIC;
    c->rank = -1;

    if (!(c->loop = ev_loop_new (EVFLAG_AUTO))) {
        errno = ENOMEM;
        goto error;
    }
    if (!(c->watchers = zhash_new ())) {
        errno = ENOMEM;
        goto error;
    }
    c->fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (c->fd < 0)
        goto error;
    ev_io_init (&c->unix_w, unix_cb, c->fd, EV_READ);

    if (!(c->putmsg = zlist_new ()))
        oom ();
    ev_zlist_init (&c->putmsg_w, putmsg_cb, c->putmsg, EV_READ);

    for (;;) {
        if (!pidcheck (pidfile))
            goto error;
        memset (&addr, 0, sizeof (struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        strncpy (addr.sun_path, sockfile, sizeof (addr.sun_path) - 1);
        if (connect (c->fd, (struct sockaddr *)&addr,
                     sizeof (struct sockaddr_un)) == 0)
            break;
        usleep (100*1000);
    }
    c->h = flux_handle_create (c, &handle_ops, flags);
    return c->h;
error:
    if (c) {
        int saved_errno = errno;
        op_fini (c);
        errno = saved_errno;
    }
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .sendmsg = op_sendmsg,
    .recvmsg = op_recvmsg,
    .putmsg = op_putmsg,
    .pushmsg = op_pushmsg,
    .purge = op_purge,
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .rank = op_rank,
    .reactor_stop = op_reactor_stop,
    .reactor_start = op_reactor_start,
    .reactor_fd_add = op_reactor_fd_add,
    .reactor_fd_remove = op_reactor_fd_remove,
    .reactor_zs_add = op_reactor_zs_add,
    .reactor_zs_remove = op_reactor_zs_remove,
    .reactor_tmout_add = op_reactor_tmout_add,
    .reactor_tmout_remove = op_reactor_tmout_remove,
    .reactor_msg_add = op_reactor_msg_add,
    .reactor_msg_remove = op_reactor_msg_remove,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
