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

/* apisrv.c - bridge unix domain API socket and zmq message broker */

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
#include <fcntl.h>
#include <czmq.h>
#include <json.h>

#include "zmsg.h"
#include "xzmalloc.h"
#include "jsonutil.h"
#include "log.h"
#include "zfd.h"

#include "flux.h"

#define LISTEN_BACKLOG      5

typedef struct {
    int listen_fd;
    zlist_t *clients;
    flux_t h;
    uid_t session_owner;
} ctx_t;

typedef struct {
    int type;
    char *topic;
} subscription_t;

typedef struct {
    int fd;
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zlist_t *subscriptions;
    zuuid_t *uuid;
    int cfd_id;
    struct ucred ucred;
} client_t;

static void client_destroy (client_t *c);

static void freectx (ctx_t *ctx)
{
    zlist_destroy (&ctx->clients);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "apisrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (!(ctx->clients = zlist_new ()))
            oom ();
        ctx->session_owner = geteuid ();
        flux_aux_set (h, "apisrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static client_t * client_create (ctx_t *ctx, int fd)
{
    client_t *c;
    socklen_t crlen = sizeof (c->ucred);
    flux_t h = ctx->h;

    c = xzmalloc (sizeof (*c));
    c->fd = fd;
    if (!(c->uuid = zuuid_new ()))
        oom ();
    c->ctx = ctx;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    if (!(c->subscriptions = zlist_new ()))
        oom ();
    if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &c->ucred, &crlen) < 0) {
        flux_log (h, LOG_ERR, "getsockopt SO_PEERCRED: %s", strerror (errno));
        goto error;
    }
    assert (crlen == sizeof (c->ucred));
    /* Deny connections by uid other than session owner for now.
     */
    if (c->ucred.uid != ctx->session_owner) {
        flux_log (h, LOG_ERR, "connect by uid=%d pid=%d denied",
                  c->ucred.uid, (int)c->ucred.pid);
        goto error;
    }
    return (c);
error:
    client_destroy (c);
    return NULL;
}

static subscription_t *subscription_create (flux_t h, int type, char *topic)
{
    subscription_t *sub = xzmalloc (sizeof (*sub));
    sub->type = type;
    sub->topic = xstrdup (topic);
    if (type == FLUX_MSGTYPE_EVENT) {
        (void)flux_event_subscribe (h, topic);
        flux_log (h, LOG_DEBUG, "event subscribe %s", topic);
    }
    return sub;
}

static void subscription_destroy (flux_t h, subscription_t *sub)
{
    if (sub->type == FLUX_MSGTYPE_EVENT) {
        (void)flux_event_unsubscribe (h, sub->topic);
        flux_log (h, LOG_DEBUG, "event unsubscribe %s", sub->topic);
    }
    free (sub->topic);
    free (sub);
}

static subscription_t *subscription_lookup (client_t *c, int type, char *topic)
{
    subscription_t *sub;

    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (sub->type == type && !strcmp (sub->topic, topic))
            return sub;
        sub = zlist_next (c->subscriptions);
    }
    return NULL;
}

static bool subscription_match (client_t *c, int type, char *topic)
{
    subscription_t *sub;

    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (sub->type == type && !strncmp (sub->topic, topic,
                                           strlen (sub->topic)))
            return true;
        sub = zlist_next (c->subscriptions);
    }
    return false;
}

static int notify_srv (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    zmsg_t *zmsg; 
    json_object *o;

    if (!(zmsg = zmsg_new ()))
        oom ();
    o = util_json_object_new_object ();
    if (zmsg_pushstr (zmsg, json_object_to_json_string (o)) < 0)
        oom ();
    json_object_put (o);
#if CZMQ_VERSION_MAJOR < 2
    if (zmsg_pushstr (zmsg, "%s.disconnect", key) < 0)
        oom ();
#else
    if (zmsg_pushstrf (zmsg, "%s.disconnect", key) < 0)
        oom ();
#endif
    if (zmsg_pushmem (zmsg, NULL, 0) < 0) /* delimiter frame */
        oom ();
    if (zmsg_pushstr (zmsg, zuuid_str (c->uuid)) < 0)
        oom ();

    flux_request_sendmsg (c->ctx->h, &zmsg);

    return 0;
}

static void client_destroy (client_t *c)
{
    subscription_t *sub;
    ctx_t *ctx = c->ctx;

    if (c->disconnect_notify) {
        zhash_foreach (c->disconnect_notify, notify_srv, c);
        zhash_destroy (&c->disconnect_notify);
    }
    if (c->subscriptions) {
        while ((sub = zlist_pop (c->subscriptions)))
            subscription_destroy (ctx->h, sub);
        zlist_destroy (&c->subscriptions);
    }
    if (c->uuid)
        zuuid_destroy (&c->uuid);
    if (c->fd != -1)
        close (c->fd);

    free (c);
}

static int client_read (ctx_t *ctx, client_t *c)
{
    zmsg_t *zmsg = NULL;
    char *name = NULL;
    int typemask;
    subscription_t *sub;

    zmsg = zfd_recv_typemask (c->fd, &typemask, true);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            flux_log (ctx->h, LOG_ERR, "API read: %s", strerror (errno));
        return -1;
    }
    if (!(typemask & FLUX_MSGTYPE_REQUEST))
        goto done; /* DROP */

    if (flux_msg_match_substr (zmsg, "api.event.subscribe.", &name)) {
        sub = subscription_create (ctx->h, FLUX_MSGTYPE_EVENT, name);
        if (zlist_append (c->subscriptions, sub) < 0)
            oom ();
    } else if (flux_msg_match_substr (zmsg, "api.event.unsubscribe.", &name)) {
        if ((sub = subscription_lookup (c, FLUX_MSGTYPE_EVENT, name)))
            zlist_remove (c->subscriptions, sub);
    } else {
        /* insert disconnect notifier before forwarding request */
        if (c->disconnect_notify) {
            char *tag = flux_msg_tag_short (zmsg);
            if (!tag)
                goto done;
            if (zhash_lookup (c->disconnect_notify, tag) == NULL) {
                if (zhash_insert (c->disconnect_notify, tag, tag) < 0)
                    oom ();
                zhash_freefn (c->disconnect_notify, tag, free);
            } else
                free (tag);
        }
        if (zmsg_pushstr (zmsg, zuuid_str (c->uuid)) < 0)
            oom ();
        flux_request_sendmsg (ctx->h, &zmsg);
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
    if (name)
        free (name);
    return 0;
}

static int client_cb (flux_t h, int fd, short revents, void *arg)
{
    client_t *c = arg;
    ctx_t *ctx = c->ctx;
    bool delete = false;

    if (revents & ZMQ_POLLIN) {
        while (client_read (ctx, c) != -1)
            ;
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            delete = true;
    }
    if (revents & ZMQ_POLLERR)
        delete = true;

    if (delete) {
        /*  Cancel this client's fd from the reactor and destroy client
         */
        flux_fdhandler_remove (h, fd, ZMQ_POLLIN | ZMQ_POLLERR);
        zlist_remove (ctx->clients, c);
        client_destroy (c);
    }
    return 0;
}

static int response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    char *uuid = NULL;
    zframe_t *zf = NULL;
    client_t *c;

    if (flux_msg_hopcount (*zmsg) != 1) {
        flux_log (ctx->h, LOG_ERR, "dropping response with bad envelope");
        goto done;
    }
    uuid = zmsg_popstr (*zmsg);
    assert (uuid != NULL);
    zf = zmsg_pop (*zmsg);
    assert (zf != NULL);
    assert (zframe_size (zf) == 0);

    c = zlist_first (ctx->clients);
    while (c && *zmsg) {
        if (!strcmp (uuid, zuuid_str (c->uuid))) {
            if (zfd_send_typemask (c->fd, FLUX_MSGTYPE_RESPONSE, zmsg) < 0)
                zmsg_destroy (zmsg);
            break;
        }
        c = zlist_next (ctx->clients);
    }
    if (*zmsg)
        zmsg_destroy (zmsg); /* discard response for unknown uuid */
    if (zf)
        zframe_destroy (&zf);
    if (uuid)
        free (uuid);
done:
    return 0;
}

static int event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    client_t *c;
    char *tag = flux_msg_tag (*zmsg);
    zmsg_t *cpy;

    if (tag) {
        c = zlist_first (ctx->clients);
        while (c) {
            if (subscription_match (c, FLUX_MSGTYPE_EVENT, tag)) {
                if (!(cpy = zmsg_dup (*zmsg)))
                    oom ();
                if (zfd_send_typemask (c->fd, FLUX_MSGTYPE_EVENT, &cpy) < 0)
                    zmsg_destroy (&cpy);
            }
            c = zlist_next (ctx->clients);
        }
        free (tag);
    }
    return 0;
}

static int listener_cb (flux_t h, int fd, short revents, void *arg)
{
    ctx_t *ctx = arg;
    int rc = 0;

    if (revents & ZMQ_POLLIN) {
        client_t *c;
        int cfd;

        if ((cfd = accept (fd, NULL, NULL)) < 0) {
            flux_log (h, LOG_ERR, "accept: %s", strerror (errno));
            goto done;
        }
        if (!(c = client_create (ctx, cfd))) {
            close (cfd);
            goto done;
        }
        if (zlist_append (ctx->clients, c) < 0)
            oom ();
        if (flux_fdhandler_add (h, cfd, ZMQ_POLLIN | ZMQ_POLLERR,
                                                    client_cb, c) < 0) {
            flux_log (h, LOG_ERR, "flux_fdhandler_add: %s", strerror (errno));
            rc = -1; /* terminate reactor */
            goto done;
        }
    }
    if (revents & ZMQ_POLLERR) {
        flux_log (h, LOG_ERR, "poll listen fd: %s", strerror (errno));
        goto done;
    }
done:
    return rc;
}

static int listener_init (ctx_t *ctx, char *sockpath)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket (AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        flux_log (ctx->h, LOG_ERR, "socket: %s", strerror (errno));
        goto done;
    }
    if (remove (sockpath) < 0 && errno != ENOENT) {
        flux_log (ctx->h, LOG_ERR, "remove %s: %s", sockpath, strerror (errno));
        goto error_close;
    }
    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, sockpath, sizeof (addr.sun_path) - 1);

    if (bind (fd, (struct sockaddr *)&addr, sizeof (struct sockaddr_un)) < 0) {
        flux_log (ctx->h, LOG_ERR, "bind: %s", strerror (errno));
        goto error_close;
    }
    if (listen (fd, LISTEN_BACKLOG) < 0) {
        flux_log (ctx->h, LOG_ERR, "listen: %s", strerror (errno));
        goto error_close;
    }
done:
    return fd;
error_close:
    close (fd);
    return -1;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,     "*",          event_cb },
    { FLUX_MSGTYPE_RESPONSE,  "*",          response_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);
    char *sockpath = NULL, *dfltpath = NULL;
    int rc = -1;

    if (!args || !(sockpath = zhash_lookup (args, "sockpath"))) {
        const char *tmpdir = getenv ("FLUX_TMPDIR");
        if (!tmpdir)
            tmpdir = getenv ("TMPDIR");
        if (!tmpdir)
            tmpdir = "/tmp";
        if (asprintf (&dfltpath, "%s/flux-api", tmpdir) < 0)
            oom ();
        sockpath = dfltpath;
    }
    if ((ctx->listen_fd = listener_init (ctx, sockpath)) < 0)
        goto done;
    if (flux_fdhandler_add (h, ctx->listen_fd, ZMQ_POLLIN | ZMQ_POLLERR,
                                                    listener_cb, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_fdhandler_add: %s", strerror (errno));
        goto done;
    }
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        goto done;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        goto done;
    }
    rc = 0;
done:
    if (dfltpath)
        free (dfltpath);
    if (ctx->listen_fd >= 0) {
        if (close (ctx->listen_fd) < 0)
            flux_log (h, LOG_ERR, "close listen_fd: %s", strerror (errno));
    }
    if (ctx->clients) {
        client_t *c;
        while ((c = zlist_pop (ctx->clients)))
            client_destroy (c);
    }
    return rc;
}

MOD_NAME ("api");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
