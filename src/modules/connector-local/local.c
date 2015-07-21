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
#include <fcntl.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/zfd.h"


#define LISTEN_BACKLOG      5

typedef struct {
    int listen_fd;
    flux_fd_watcher_t *listen_w;
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
    flux_fd_watcher_t *w;
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zlist_t *subscriptions;
    zuuid_t *uuid;
    int cfd_id;
    struct ucred ucred;
} client_t;

struct disconnect_notify {
    char *topic;
    uint32_t nodeid;
    int flags;
};

static void client_destroy (client_t *c);
static void client_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                       int revents, void *arg);

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    zlist_destroy (&ctx->clients);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "flux::local_connector");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (!(ctx->clients = zlist_new ()))
            oom ();
        ctx->session_owner = geteuid ();
        flux_aux_set (h, "flux::local_connector", ctx, freectx);
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
    if (!(c->w = flux_fd_watcher_create (fd, FLUX_POLLIN | FLUX_POLLERR,
                                         client_cb, c))) {
        flux_log (h, LOG_ERR, "flux_fd_watcher_create: %s", strerror (errno));
        goto error;
    }
    return (c);
error:
    client_destroy (c);
    return NULL;
}

static subscription_t *subscription_create (flux_t h, int type,
                                            const char *topic)
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

static subscription_t *subscription_lookup (client_t *c, int type,
                                            const char *topic)
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

static bool subscription_match (client_t *c, const flux_msg_t *msg)
{
    subscription_t *sub;
    const char *topic;

    if (flux_msg_get_topic (msg, &topic) < 0)
        return false;
    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (!strncmp (topic, sub->topic, strlen (sub->topic)))
            return true;
        sub = zlist_next (c->subscriptions);
    }
    return false;
}

static void disconnect_destroy (client_t *c, struct disconnect_notify *d)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (msg, d->topic) < 0)
        goto done;
    if (flux_msg_enable_route (msg) < 0)
        goto done;
    if (flux_msg_push_route (msg, zuuid_str (c->uuid)) < 0)
        goto done;
    if (flux_msg_set_nodeid (msg, d->nodeid, d->flags) < 0)
        goto done;
    (void)flux_send (c->ctx->h, msg, 0);
done:
    flux_msg_destroy (msg);
    free (d->topic);
    free (d);
}

static int disconnect_update (client_t *c, const flux_msg_t *msg)
{
    char *p;
    char *key = NULL;
    char *svc = NULL;
    const char *topic;
    uint32_t nodeid;
    int flags;
    struct disconnect_notify *d;
    int rc = -1;

    if (flux_msg_get_topic (msg, &topic) < 0)
        goto done;
    if (flux_msg_get_nodeid (msg, &nodeid, &flags) < 0)
        goto done;
    svc = xstrdup (topic);
    if ((p = strchr (svc, '.')))
        *p = '\0';
    key = xasprintf ("%s:%u:%d", svc, nodeid, flags);
    if (!zhash_lookup (c->disconnect_notify, key)) {
        d = xzmalloc (sizeof (*d));
        d->nodeid = nodeid;
        d->flags = flags;
        d->topic = xasprintf ("%s.disconnect", svc);
        zhash_update (c->disconnect_notify, key, d);
    }
    rc = 0;
done:
    if (svc)
        free (svc);
    if (key)
        free (key);
    return rc;
}

static void client_destroy (client_t *c)
{
    subscription_t *sub;
    ctx_t *ctx = c->ctx;

    if (c->disconnect_notify) {
        struct disconnect_notify *d;

        d = zhash_first (c->disconnect_notify);
        while (d) {
            disconnect_destroy (c, d);
            d = zhash_next (c->disconnect_notify);
        }
        zhash_destroy (&c->disconnect_notify);
    }
    if (c->subscriptions) {
        while ((sub = zlist_pop (c->subscriptions)))
            subscription_destroy (ctx->h, sub);
        zlist_destroy (&c->subscriptions);
    }
    if (c->uuid)
        zuuid_destroy (&c->uuid);
    flux_fd_watcher_destroy (c->w);
    if (c->fd != -1)
        close (c->fd);

    free (c);
}

static bool match_substr (zmsg_t *zmsg, const char *topic, const char **rest)
{
    const char *s;
    if (flux_msg_get_topic (zmsg, &s) < 0)
        return false;
    if (strncmp(topic, s, strlen (topic)) != 0)
        return false;
    if (rest)
        *rest = s + strlen (topic);
    return true;
}

static int client_read (ctx_t *ctx, client_t *c)
{
    flux_msg_t *msg;
    subscription_t *sub;
    int type;

    msg = zfd_recv (c->fd, true); /* fails with EPROTO on EOF */
    if (!msg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            flux_log (ctx->h, LOG_ERR, "recv: %s", strerror (errno));
        return -1;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        flux_log (ctx->h, LOG_ERR, "get_type:  %s", strerror (errno));
        goto done;
    }
    switch (type) {
        const char *name;
        case FLUX_MSGTYPE_REQUEST:
            if (match_substr (msg, "api.event.subscribe.", &name)) {
                sub = subscription_create (ctx->h, FLUX_MSGTYPE_EVENT, name);
                if (zlist_append (c->subscriptions, sub) < 0)
                    oom ();
                goto done;
            }
            if (match_substr (msg, "api.event.unsubscribe.", &name)) {
                if ((sub = subscription_lookup (c, FLUX_MSGTYPE_EVENT, name)))
                    zlist_remove (c->subscriptions, sub);
                goto done;
            }
            /* insert disconnect notifier before forwarding request */
            if (c->disconnect_notify && disconnect_update (c, msg) < 0) {
                flux_log (ctx->h, LOG_ERR, "disconnect_update:  %s",
                          strerror (errno));
                goto done;
            }
            if (flux_msg_push_route (msg, zuuid_str (c->uuid)) < 0)
                oom (); /* FIXME */
            if (flux_send (ctx->h, msg, 0) < 0)
                err ("%s: flux_send", __FUNCTION__);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (flux_send (ctx->h, msg, 0) < 0)
                err ("%s: flux_send", __FUNCTION__);
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "drop unexpected %s",
                      flux_msg_typestr (type));
            break;
    }
done:
    flux_msg_destroy (msg);
    return 0;
}

static void client_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                       int revents, void *arg)
{
    client_t *c = arg;
    ctx_t *ctx = c->ctx;
    bool delete = false;

    if (revents & FLUX_POLLIN) {
        while (client_read (ctx, c) != -1)
            ;
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            delete = true;
    }
    if (revents & FLUX_POLLERR) {
        delete = true;
    }

    if (delete) {
        /*  Cancel this client's fd from the reactor and destroy client
         */
        flux_fd_watcher_stop (h, w);
        zlist_remove (ctx->clients, c);
        client_destroy (c);
    }
}

/* Received response message from broker.
 * Look up the sender uuid in clients hash and deliver.
 * Responses for disconnected clients are silently discarded.
 */
static void response_cb (flux_t h, flux_msg_watcher_t *w,
                         const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    char *uuid = NULL;
    client_t *c;
    flux_msg_t *cpy = flux_msg_copy (msg, true);

    if (!cpy)
        oom ();
    if (flux_msg_pop_route (cpy, &uuid) < 0)
        goto done;
    if (flux_msg_clear_route (cpy) < 0)
        goto done;
    c = zlist_first (ctx->clients);
    while (c) {
        if (!strcmp (uuid, zuuid_str (c->uuid))) {
            if (zfd_send (c->fd, cpy) < 0)
                errno = 0; /* ignore send errors, let FLUX_POLLERR handle */
            break;
        }
        c = zlist_next (ctx->clients);
    }
    if (uuid)
        free (uuid);
done:
    flux_msg_destroy (cpy);
}

/* Received an event message from broker.
 * Find all subscribers and deliver.
 */
static void event_cb (flux_t h, flux_msg_watcher_t *w,
                      const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    client_t *c;

    c = zlist_first (ctx->clients);
    while (c) {
        if (subscription_match (c, msg)) {
            if (zfd_send (c->fd, (zmsg_t *)msg) < 0)
                errno = 0; /* ignore send errors, let POLL_ERR handle */
        }
        c = zlist_next (ctx->clients);
    }
}

/* Accept a connection from new client.
 */
static void listener_cb (flux_t h, flux_fd_watcher_t *w,
                         int fd, int revents, void *arg)
{
    ctx_t *ctx = arg;

    if (revents & FLUX_POLLIN) {
        client_t *c;
        int cfd;

        if ((cfd = accept4 (fd, NULL, NULL, SOCK_CLOEXEC)) < 0) {
            flux_log (h, LOG_ERR, "accept: %s", strerror (errno));
            goto done;
        }
        if (!(c = client_create (ctx, cfd))) {
            close (cfd);
            goto done;
        }
        if (zlist_append (ctx->clients, c) < 0)
            oom ();
        flux_fd_watcher_start (h, c->w);
    }
    if (revents & ZMQ_POLLERR) {
        flux_log (h, LOG_ERR, "poll listen fd: %s", strerror (errno));
    }
done:
    return;
}

static int listener_init (ctx_t *ctx, char *sockpath)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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

static struct flux_msghandler htab[] = {
    { FLUX_MSGTYPE_EVENT,     NULL,          event_cb },
    { FLUX_MSGTYPE_RESPONSE,  NULL,          response_cb },
    FLUX_MSGHANDLER_TABLE_END
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, int argc, char **argv)
{
    ctx_t *ctx = getctx (h);
    char *sockpath = NULL, *dfltpath = NULL;
    int rc = -1;

    /* Parse args.
     */
    if (argc > 0)
        sockpath = argv[0];
    if (!sockpath)
        sockpath = dfltpath = xasprintf ("%s/flux-api", flux_get_tmpdir ());

    /* Create listen socket and watcher to handle new connections
     */
    if ((ctx->listen_fd = listener_init (ctx, sockpath)) < 0)
        goto done;
    if (!(ctx->listen_w = flux_fd_watcher_create (ctx->listen_fd,
                                           FLUX_POLLIN | FLUX_POLLERR,
                                           listener_cb, ctx))) {
        flux_log (h, LOG_ERR, "flux_fd_watcher_create: %s", strerror (errno));
        goto done;
    }
    flux_fd_watcher_start (h, ctx->listen_w);

    /* Create/start event/response message watchers
     */
    if (flux_msg_watcher_addvec (h, htab, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msg_watcher_addvec: %s", strerror (errno));
        goto done;
    }

    /* Start reactor
     */
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        goto done;
    }
    rc = 0;
done:
    if (dfltpath)
        free (dfltpath);
    flux_msg_watcher_delvec (h, htab);
    flux_fd_watcher_destroy (ctx->listen_w);
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

MOD_NAME ("connector-local");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
