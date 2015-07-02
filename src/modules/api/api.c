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

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
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
        flux_aux_set (h, "apisrv", ctx, freectx);
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

static bool subscription_match (client_t *c, int type, zmsg_t *zmsg)
{
    subscription_t *sub;
    const char *topic;

    if (flux_msg_get_topic (zmsg, &topic) < 0)
        return false;
    sub = zlist_first (c->subscriptions);
    while (sub) {
        if (sub->type == type && !strncmp (topic, sub->topic,
                                           strlen (sub->topic)))
            return true;
        sub = zlist_next (c->subscriptions);
    }
    return false;
}

static int notify_srv (const char *key, void *item, void *arg)
{
    client_t *c = arg;
    char *topic = xasprintf ("%s.disconnect", key);
    zmsg_t *zmsg;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, topic) < 0)
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    if (flux_msg_push_route (zmsg, zuuid_str (c->uuid)) < 0)
        goto done;
    flux_sendmsg (c->ctx->h, &zmsg);
done:
    zmsg_destroy (&zmsg);
    free (topic);
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
    zmsg_t *zmsg = NULL;
    subscription_t *sub;
    int type;

    zmsg = zfd_recv (c->fd, true);
    if (!zmsg) {
        if (errno != ECONNRESET && errno != EWOULDBLOCK && errno != EPROTO)
            flux_log (ctx->h, LOG_ERR, "recv: %s", strerror (errno));
        return -1;
    }
    if (flux_msg_get_type (zmsg, &type) < 0) {
        flux_log (ctx->h, LOG_ERR, "get_type:  %s", strerror (errno));
        goto done;
    }
    switch (type) {
        const char *name;
        case FLUX_MSGTYPE_REQUEST:
            if (match_substr (zmsg, "api.event.subscribe.", &name)) {
                sub = subscription_create (ctx->h, FLUX_MSGTYPE_EVENT, name);
                if (zlist_append (c->subscriptions, sub) < 0)
                    oom ();
                goto done;
            }
            if (match_substr (zmsg, "api.event.unsubscribe.", &name)) {
                if ((sub = subscription_lookup (c, FLUX_MSGTYPE_EVENT, name)))
                    zlist_remove (c->subscriptions, sub);
                goto done;
            }
            /* insert disconnect notifier before forwarding request */
            if (c->disconnect_notify) {
                const char *topic;
                char *p, *cpy = NULL;
                if (flux_msg_get_topic (zmsg, &topic) < 0)
                    goto done;
                cpy = xstrdup (topic);
                if ((p = strchr (cpy, '.')))
                    *p = '\0';
                if (zhash_lookup (c->disconnect_notify, cpy) == NULL) {
                    if (zhash_insert (c->disconnect_notify, cpy, cpy) < 0)
                        oom ();
                    zhash_freefn (c->disconnect_notify, cpy, free);
                } else
                    free (cpy);
            }
            if (flux_msg_push_route (zmsg, zuuid_str (c->uuid)) < 0)
                oom (); /* FIXME */
            if (flux_sendmsg (ctx->h, &zmsg) < 0)
                err ("%s: flux_sendmsg", __FUNCTION__);
            break;
        case FLUX_MSGTYPE_EVENT:
            if (flux_sendmsg (ctx->h, &zmsg) < 0)
                err ("%s: flux_sendmsg", __FUNCTION__);
            break;
        default:
            flux_log (ctx->h, LOG_ERR, "drop unexpected %s",
                      flux_msg_typestr (type));
            break;
    }
done:
    if (zmsg)
        zmsg_destroy (&zmsg);
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

/* Received response message from broker.
 * Look up the sender uuid in clients hash and deliver.
 * Responses for disconnected clients are silently discarded.
 */
static int response_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    char *uuid = NULL;
    client_t *c;

    if (flux_msg_pop_route (*zmsg, &uuid) < 0) {
        err ("dropping mangled response (no routes)");
        zmsg_destroy (zmsg);
        goto done;
    }
    if (flux_msg_clear_route (*zmsg) < 0) {
        err ("dropping mangled response");
        zmsg_destroy (zmsg);
        goto done;
    }
    c = zlist_first (ctx->clients);
    while (c && *zmsg) {
        if (!strcmp (uuid, zuuid_str (c->uuid))) {
            if (zfd_send (c->fd, *zmsg) < 0)
                errno = 0; /* ignore send errors, let POLL_ERR handle */
            break;
        }
        c = zlist_next (ctx->clients);
    }
    if (uuid)
        free (uuid);
done:
    zmsg_destroy (zmsg);
    return 0;
}

/* Received an event message from broker.
 * Find all subscribers and deliver.
 */
static int event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    client_t *c;

    if (*zmsg) {
        c = zlist_first (ctx->clients);
        while (c) {
            if (subscription_match (c, FLUX_MSGTYPE_EVENT, *zmsg)) {
                if (zfd_send (c->fd, *zmsg) < 0)
                    errno = 0; /* ignore send errors, let POLL_ERR handle */
            }
            c = zlist_next (ctx->clients);
        }
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
        if (asprintf (&dfltpath, "%s/flux-api", flux_get_tmpdir ()) < 0)
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
