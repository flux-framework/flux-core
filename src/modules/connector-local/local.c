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
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"


#define LISTEN_BACKLOG      5

typedef struct {
    int listen_fd;
    flux_fd_watcher_t *listen_w;
    zlist_t *clients;
    flux_t h;
    uid_t session_owner;
    zhash_t *subscriptions;
} ctx_t;

typedef void (*unsubscribe_f)(void *handle, const char *topic);

typedef struct {
    char *topic;
    int usecount;
    unsubscribe_f unsubscribe;
    void *handle;
} subscription_t;

typedef struct {
    int fd;
    flux_fd_watcher_t *inw;
    flux_fd_watcher_t *outw;
    struct flux_msg_iobuf inbuf;
    struct flux_msg_iobuf outbuf;
    zlist_t *outqueue;  /* queue of outbound flux_msg_t */
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zhash_t *subscriptions;
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
static void client_read_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                            int revents, void *arg);
static void client_write_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                            int revents, void *arg);

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    zlist_destroy (&ctx->clients);
    zhash_destroy (&ctx->subscriptions);
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
        if (!(ctx->subscriptions = zhash_new ()))
            oom ();
        ctx->session_owner = geteuid ();
        flux_aux_set (h, "flux::local_connector", ctx, freectx);
    }

    return ctx;
}

static int set_nonblock (int fd, bool nonblock)
{
    int flags = fcntl (fd, F_GETFL);
    if (flags < 0)
        return -1;
    if (nonblock)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    if (fcntl (fd, F_SETFL, flags) < 0)
        return -1;
    return 0;
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
    if (!(c->subscriptions = zhash_new ()))
        oom ();
    if (!(c->outqueue = zlist_new ()))
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
    c->inw = flux_fd_watcher_create (fd, FLUX_POLLIN, client_read_cb, c);
    c->outw = flux_fd_watcher_create (fd, FLUX_POLLOUT, client_write_cb, c);
    if (!c->inw || !c->outw) {
        flux_log (h, LOG_ERR, "flux_fd_watcher_create: %s", strerror (errno));
        goto error;
    }
    flux_fd_watcher_start (h, c->inw);
    flux_msg_iobuf_init (&c->inbuf);
    flux_msg_iobuf_init (&c->outbuf);
    if (set_nonblock (c->fd, true) < 0) {
        flux_log (h, LOG_ERR, "set_nonblock: %s", strerror (errno));
        goto error;
    }

    return (c);
error:
    client_destroy (c);
    return NULL;
}

static int client_send_try (client_t *c)
{
    flux_msg_t *msg = zlist_head (c->outqueue);

    if (msg) {
        if (flux_msg_sendfd (c->fd, msg, &c->outbuf) < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                return -1;
            //flux_log (c->ctx->h, LOG_DEBUG, "send: client not ready");
            flux_fd_watcher_start (c->ctx->h, c->outw);
            errno = 0;
        } else {
            msg = zlist_pop (c->outqueue);
            flux_msg_destroy (msg);
        }
    }
    return 0;
}

static int client_send_nocopy (client_t *c, flux_msg_t **msg)
{
    if (zlist_append (c->outqueue, *msg) < 0) {
        errno = ENOMEM;
        return -1;
    }
    *msg = NULL;
    return client_send_try (c);
}

static int client_send (client_t *c, const flux_msg_t *msg)
{
    flux_msg_t *cpy = flux_msg_copy (msg, true);
    int rc;

    if (!cpy) {
        errno = ENOMEM;
        return -1;
    }
    rc = client_send_nocopy (c, &cpy);
    flux_msg_destroy (cpy);
    return rc;
}

static subscription_t *subscription_create (const char *topic)
{
    subscription_t *sub = xzmalloc (sizeof (*sub));
    sub->topic = xstrdup (topic);
    return sub;
}

static void subscription_destroy (void *data)
{
    subscription_t *sub = data;
    if (sub->unsubscribe)
        (void) sub->unsubscribe (sub->handle, sub->topic);
    free (sub->topic);
    free (sub);
}

static int global_subscribe (ctx_t *ctx, const char *topic)
{
    subscription_t *sub;
    int rc = -1;

    if (!(sub = zhash_lookup (ctx->subscriptions, topic))) {
        if (flux_event_subscribe (ctx->h, topic) < 0) {
            flux_log (ctx->h, LOG_ERR, "%s: flux_event_subscribe %s: %s",
                      __FUNCTION__, topic, strerror (errno));
            goto done;
        } 
        sub = subscription_create (topic);
        sub->unsubscribe = (unsubscribe_f) flux_event_unsubscribe;
        sub->handle = ctx->h;
        zhash_update (ctx->subscriptions, topic, sub);
        zhash_freefn (ctx->subscriptions, topic, subscription_destroy);
        //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, topic);
    }
    sub->usecount++;
    rc = 0;
done:
    return rc;
}

static int global_unsubscribe (ctx_t *ctx, const char *topic)
{
    subscription_t *sub;
    int rc = -1;

    if (!(sub = zhash_lookup (ctx->subscriptions, topic)))
        goto done;
    if (--sub->usecount == 0) {
        zhash_delete (ctx->subscriptions, topic);
        //flux_log (ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, topic);
    }
    rc = 0;
done:
    return rc;
}

static int client_subscribe (client_t *c, const char *topic)
{
    subscription_t *sub;
    int rc = -1;

    if (!(sub = zhash_lookup (c->subscriptions, topic))) {
        if (global_subscribe (c->ctx, topic) < 0)
            goto done;
        sub = subscription_create (topic);
        sub->unsubscribe = (unsubscribe_f) global_unsubscribe;
        sub->handle = c->ctx;
        zhash_update (c->subscriptions, topic, sub);
        zhash_freefn (c->subscriptions, topic, subscription_destroy);
        //flux_log (c->ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, topic);
    }
    sub->usecount++;
    rc = 0;
done:
    return rc;
}

static int client_unsubscribe (client_t *c, const char *topic)
{
    subscription_t *sub;
    int rc = -1;

    if (!(sub = zhash_lookup (c->subscriptions, topic)))
        goto done;
    if (--sub->usecount == 0) {
        zhash_delete (c->subscriptions, topic);
        //flux_log (c->ctx->h, LOG_DEBUG, "%s: %s", __FUNCTION__, topic);
    }
    rc = 0;
done:
    return rc;
}

int sub_request (client_t *c, const flux_msg_t *msg, bool subscribe)
{
    const char *json_str, *topic;
    JSON in = NULL;
    int rc = -1;

    if (flux_request_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (!(in = Jfromstr (json_str)) || !Jget_str (in, "topic", &topic)) {
        errno = EPROTO;
        goto done;
    }
    if (subscribe)
        rc = client_subscribe (c, topic);
    else
        rc = client_unsubscribe (c, topic);
done:
    Jput (in);
    return rc;
}

static bool client_is_subscribed (client_t *c, const char *topic)
{
    subscription_t *sub;

    if (zhash_lookup (c->subscriptions, topic))
        return true;
    sub = zhash_first (c->subscriptions);
    while (sub) {
        if (!strncmp (topic, sub->topic, strlen (sub->topic)))
            return true;
        sub = zhash_next (c->subscriptions);
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
    zhash_destroy (&c->subscriptions);
    if (c->uuid)
        zuuid_destroy (&c->uuid);
    if (c->outqueue) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (c->outqueue)))
            flux_msg_destroy (msg);
        zlist_destroy (&c->outqueue);
    }
    flux_fd_watcher_stop (ctx->h, c->outw);
    flux_fd_watcher_destroy (c->outw);
    flux_msg_iobuf_clean (&c->outbuf);

    flux_fd_watcher_stop (ctx->h, c->inw);
    flux_fd_watcher_destroy (c->inw);
    flux_msg_iobuf_clean (&c->inbuf);

    if (c->fd != -1)
        close (c->fd);

    free (c);
}

static void client_write_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                             int revents, void *arg)
{
    client_t *c = arg;

    if (revents & FLUX_POLLERR)
        goto disconnect;
    if (revents & FLUX_POLLOUT) {
        if (client_send_try (c) < 0)
            goto disconnect;
        //flux_log (h, LOG_DEBUG, "send: client ready");
    }
    if (zlist_size (c->outqueue) == 0)
        flux_fd_watcher_stop (h, w);
    return;
disconnect:
    zlist_remove (c->ctx->clients, c);
    client_destroy (c);
}

static bool internal_request (client_t *c, const flux_msg_t *msg)
{
    const char *topic;
    int rc = -1;
    flux_msg_t *rmsg = NULL;
    uint32_t matchtag;

    if (flux_msg_get_topic (msg, &topic) < 0
            || flux_msg_get_matchtag (msg, &matchtag) < 0)
        return false;
    else if (!strcmp (topic, "local.sub"))
        rc = sub_request (c, msg, true);
    else if (!strcmp (topic, "local.unsub"))
        rc = sub_request (c, msg, false);
    else
        return false;

    /* Respond to client
     */
    if (!(rmsg = flux_response_encode (topic, rc < 0 ? errno : 0, NULL))
                    || flux_msg_set_matchtag (rmsg, matchtag) < 0)
        flux_log (c->ctx->h, LOG_ERR, "%s: encoding response: %s",
                  __FUNCTION__, strerror (errno));
        
    else if (client_send_nocopy (c, &rmsg) < 0)
        flux_log (c->ctx->h, LOG_ERR, "%s: client_send_nocopy: %s",
                  __FUNCTION__, strerror (errno));
    flux_msg_destroy (rmsg);
    return true;
}

static void client_read_cb (flux_t h, flux_fd_watcher_t *w, int fd,
                            int revents, void *arg)
{
    client_t *c = arg;
    flux_msg_t *msg = NULL;
    int type;

    if (revents & FLUX_POLLERR)
        goto disconnect;
    if (!(revents & FLUX_POLLIN))
        return;
    /* EPROTO, ECONNRESET are normal disconnect errors
     * EWOULDBLOCK, EAGAIN stores state in c->inbuf for continuation
     */
    //flux_log (h, LOG_DEBUG, "recv: client ready");
    if (!(msg = flux_msg_recvfd (c->fd, &c->inbuf))) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) {
            //flux_log (h, LOG_DEBUG, "recv: client not ready");
            return;
        }
        if (errno != ECONNRESET && errno != EPROTO)
            flux_log (h, LOG_ERR, "flux_msg_recvfd: %s", strerror (errno));
        goto disconnect;
    }
    if (flux_msg_get_type (msg, &type) < 0) {
        flux_log (h, LOG_ERR, "flux_msg_get_type: %s", strerror (errno));
        goto disconnect;
    }
    switch (type) {
        case FLUX_MSGTYPE_REQUEST:
            if (!internal_request (c, msg)) {
                /* insert disconnect notifier before forwarding request */
                if (c->disconnect_notify && disconnect_update (c, msg) < 0) {
                    flux_log (h, LOG_ERR, "disconnect_update:  %s",
                              strerror (errno));
                    goto disconnect;
                }
                if (flux_msg_push_route (msg, zuuid_str (c->uuid)) < 0)
                    oom (); /* FIXME */
                if (flux_send (h, msg, 0) < 0)
                    err ("%s: flux_send", __FUNCTION__);
            }
            break;
        case FLUX_MSGTYPE_EVENT:
            if (flux_send (h, msg, 0) < 0)
                err ("%s: flux_send", __FUNCTION__);
            break;
        default:
            flux_log (h, LOG_ERR, "drop unexpected %s",
                      flux_msg_typestr (type));
            break;
    }
    flux_msg_destroy (msg);
    return;
disconnect:
    flux_msg_destroy (msg);
    zlist_remove (c->ctx->clients, c);
    client_destroy (c);
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
    if (!uuid) {
        const char *topic = NULL;
        (void) flux_msg_get_topic (msg, &topic);
        flux_log (h, LOG_ERR, "%s: topic %s: missing sender uuid",
                  __FUNCTION__, topic ? topic : "NULL");
        goto done;
    }
    if (flux_msg_clear_route (cpy) < 0)
        goto done;
    c = zlist_first (ctx->clients);
    while (c) {
        if (!strcmp (uuid, zuuid_str (c->uuid))) {
            if (client_send_nocopy (c, &cpy) < 0) { /* FIXME handle errors */
                flux_log (h, LOG_ERR, "%s: client_send %s: %s",
                          __FUNCTION__, zuuid_str (c->uuid), strerror (errno));
                errno = 0;
            }
            break;
        }
        c = zlist_next (ctx->clients);
    }
done:
    if (uuid)
        free (uuid);
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
    const char *topic;
    int count = 0;

    if (flux_msg_get_topic (msg, &topic) < 0) {
        flux_log (h, LOG_ERR, "%s: dropped: %s",
                  __FUNCTION__, strerror (errno));
        return;
    }
    c = zlist_first (ctx->clients);
    while (c) {
        if (client_is_subscribed (c, topic)) {
            if (client_send (c, msg) < 0) { /* FIXME handle errors */
                flux_log (h, LOG_ERR, "%s: client_send %s: %s",
                          __FUNCTION__, zuuid_str (c->uuid), strerror (errno));
                errno = 0;
            }
            count++;
        }
        c = zlist_next (ctx->clients);
    }
    //flux_log (h, LOG_DEBUG, "%s: %s to %d clients", __FUNCTION__, topic, count);
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
    cleanup_push_string(cleanup_file, sockpath);
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
    char sockpath[PATH_MAX + 1];
    char *local_uri = NULL;
    char *tmpdir;
    int rc = -1;

    if (!(local_uri = flux_getattr (h, FLUX_NODEID_ANY, "local-uri"))) {
        flux_log (h, LOG_ERR, "flux_getattr local-uri: %s", strerror (errno));
        goto done;
    }
    if (!(tmpdir = strstr (local_uri, "local://"))) {
        flux_log (h, LOG_ERR, "malformed local-uri");
        goto done;
    }
    tmpdir += strlen ("local://");
    snprintf (sockpath, sizeof (sockpath), "%s/local", tmpdir);

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
    if (local_uri)
        free (local_uri);
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
