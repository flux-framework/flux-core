/*****************************************************************************\
 *  Copyright (c) 2015 Lawrence Livermore National Security, LLC.  Produced at
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
# include "config.h"
#endif
#include "builtin.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <fcntl.h>
#include <argz.h>
#include <glob.h>
#include <czmq.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/cleanup.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libsubprocess/subprocess.h"

#define LISTEN_BACKLOG      5

typedef struct {
    int listen_fd;
    flux_watcher_t *listen_w;
    zlist_t *clients;
    flux_t h;
    flux_reactor_t *reactor;
    uid_t session_owner;
    zhash_t *subscriptions;
    struct subprocess_manager *sm;
    struct subprocess *p;
    bool oneshot;
    int exit_code;
} ctx_t;

typedef void (*unsubscribe_f)(void *handle, const char *topic);

typedef struct {
    char *topic;
    int usecount;
    unsubscribe_f unsubscribe;
    void *handle;
} subscription_t;

typedef struct {
    int rfd;
    int wfd;
    flux_watcher_t *inw;
    flux_watcher_t *outw;
    struct flux_msg_iobuf inbuf;
    struct flux_msg_iobuf outbuf;
    zlist_t *outqueue;  /* queue of outbound flux_msg_t */
    ctx_t *ctx;
    zhash_t *disconnect_notify;
    zhash_t *subscriptions;
    zuuid_t *uuid;
} client_t;

struct disconnect_notify {
    char *topic;
    uint32_t nodeid;
    int flags;
    client_t *c;
};

static void client_destroy (client_t *c);
static void client_read_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg);
static void client_write_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg);

static void ctx_destroy (ctx_t *ctx)
{
    if (ctx) {
        zlist_destroy (&ctx->clients);
        zhash_destroy (&ctx->subscriptions);
        if (ctx->sm)
            subprocess_manager_destroy (ctx->sm);
        if (ctx->reactor)
            flux_reactor_destroy (ctx->reactor);
        free (ctx);
    }
}

static ctx_t *ctx_create (flux_t h)
{
    ctx_t *ctx = xzmalloc (sizeof (*ctx));
    ctx->h = h;
    if (!(ctx->reactor = flux_reactor_create (SIGCHLD)))
        err_exit ("flux_reactor_create");
    if (flux_set_reactor (h, ctx->reactor) < 0)
        err_exit ("flux_set_reactor");
    if (!(ctx->clients = zlist_new ()))
        oom ();
    if (!(ctx->subscriptions = zhash_new ()))
        oom ();
    ctx->session_owner = geteuid ();
    if (!(ctx->sm = subprocess_manager_create ()))
        err_exit ("subprocess_manager_create");
    if (subprocess_manager_set (ctx->sm, SM_REACTOR, ctx->reactor) < 0)
        err_exit ("subprocess_manager_set reactor");
    subprocess_manager_set (ctx->sm, SM_WAIT_FLAGS, WNOHANG);

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

static client_t * client_create (ctx_t *ctx, int rfd, int wfd)
{
    client_t *c;
    flux_t h = ctx->h;

    c = xzmalloc (sizeof (*c));
    c->rfd = rfd;
    c->wfd = wfd;
    if (!(c->uuid = zuuid_new ()))
        oom ();
    c->ctx = ctx;
    if (!(c->disconnect_notify = zhash_new ()))
        oom ();
    if (!(c->subscriptions = zhash_new ()))
        oom ();
    if (!(c->outqueue = zlist_new ()))
        oom ();
    c->inw = flux_fd_watcher_create (ctx->reactor, c->rfd,
                                     FLUX_POLLIN, client_read_cb, c);
    c->outw = flux_fd_watcher_create (ctx->reactor, c->wfd,
                                      FLUX_POLLOUT, client_write_cb, c);
    if (!c->inw || !c->outw) {
        flux_log (h, LOG_ERR, "flux_fd_watcher_create: %s", strerror (errno));
        goto error;
    }
    flux_watcher_start (c->inw);
    flux_msg_iobuf_init (&c->inbuf);
    flux_msg_iobuf_init (&c->outbuf);
    if (set_nonblock (c->rfd, true) < 0) {
        flux_log (h, LOG_ERR, "set_nonblock: %s", strerror (errno));
        goto error;
    }
    if (c->wfd != c->rfd && set_nonblock (c->wfd, true) < 0) {
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
        if (flux_msg_sendfd (c->wfd, msg, &c->outbuf) < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                return -1;
            //flux_log (c->ctx->h, LOG_DEBUG, "send: client not ready");
            flux_watcher_start (c->outw);
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
        /* N.B. t1008-proxy.t looks for this log message */ 
        flux_log (ctx->h, LOG_DEBUG, "subscribe %s", topic);
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
        /* N.B. t1008-proxy.t looks for this log message */ 
        flux_log (ctx->h, LOG_DEBUG, "unsubscribe %s", topic);
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

static int disconnect_sendmsg (struct disconnect_notify *d)
{
    int rc = -1;
    flux_msg_t *msg = NULL;

    if (!d || !d->topic || !d->c) {
        errno = EINVAL;
        goto done;
    }
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (msg, d->topic) < 0)
        goto done;
    if (flux_msg_enable_route (msg) < 0)
        goto done;
    if (flux_msg_push_route (msg, zuuid_str (d->c->uuid)) < 0)
        goto done;
    if (flux_msg_set_nodeid (msg, d->nodeid, d->flags) < 0)
        goto done;
    if (flux_send (d->c->ctx->h, msg, 0) < 0) {
        flux_log_error (d->c->ctx->h, "%s flux_send disconnect for %s",
                        __FUNCTION__, zuuid_str (d->c->uuid));
    }
    rc = 0;
done:
    flux_msg_destroy (msg);
    return rc;
}

static void disconnect_destroy (void *arg)
{
    struct disconnect_notify *d = arg;

    if (d) {
        (void)disconnect_sendmsg (d);
        if (d->topic)
            free (d->topic);
        free (d);
    }
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
        d->c = c;
        d->nodeid = nodeid;
        d->flags = flags;
        d->topic = xasprintf ("%s.disconnect", svc);
        zhash_update (c->disconnect_notify, key, d);
        zhash_freefn (c->disconnect_notify, key, disconnect_destroy);
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
    zhash_destroy (&c->disconnect_notify);
    zhash_destroy (&c->subscriptions);
    if (c->uuid)
        zuuid_destroy (&c->uuid);
    if (c->outqueue) {
        flux_msg_t *msg;
        while ((msg = zlist_pop (c->outqueue)))
            flux_msg_destroy (msg);
        zlist_destroy (&c->outqueue);
    }
    flux_watcher_stop (c->outw);
    flux_watcher_destroy (c->outw);
    flux_msg_iobuf_clean (&c->outbuf);

    flux_watcher_stop (c->inw);
    flux_watcher_destroy (c->inw);
    flux_msg_iobuf_clean (&c->inbuf);

    if (c->rfd != -1)
        close (c->rfd);
    if (c->wfd != -1 && c->wfd != c->rfd)
        close (c->wfd);

    free (c);
}

static void client_write_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    client_t *c = arg;
    ctx_t *ctx = c->ctx;

    if (revents & FLUX_POLLERR)
        goto disconnect;
    if (revents & FLUX_POLLOUT) {
        if (client_send_try (c) < 0)
            goto disconnect;
        //flux_log (h, LOG_DEBUG, "send: client ready");
    }
    if (zlist_size (c->outqueue) == 0)
        flux_watcher_stop (w);
    return;
disconnect:
    zlist_remove (c->ctx->clients, c);
    client_destroy (c);
    if (ctx->oneshot)
        flux_reactor_stop (r);
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

static void client_read_cb (flux_reactor_t *r, flux_watcher_t *w,
                            int revents, void *arg)
{
    client_t *c = arg;
    ctx_t *ctx = c->ctx;
    flux_t h = ctx->h;
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
    if (!(msg = flux_msg_recvfd (c->rfd, &c->inbuf))) {
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
    zlist_remove (ctx->clients, c);
    client_destroy (c);
    if (ctx->oneshot)
        flux_reactor_stop (r);
}

/* Received response message from broker.
 * Look up the sender uuid in clients hash and deliver.
 * Responses for disconnected clients are silently discarded.
 */
static void response_cb (flux_t h, flux_msg_handler_t *w,
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
    c = zlist_first (ctx->clients);
    while (c) {
        if (!strcmp (uuid, zuuid_str (c->uuid))) {
            if (client_send_nocopy (c, &cpy) < 0) { /* FIXME handle errors */
                int type = FLUX_MSGTYPE_ANY;
                const char *topic = "unknown";
                (void)flux_msg_get_type (msg, &type);
                (void)flux_msg_get_topic (msg, &topic);
                flux_log (h, LOG_ERR, "send %s %s to client %.*s: %s",
                          topic, flux_msg_typestr (type),
                          5, zuuid_str (c->uuid),
                          strerror (errno));
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
static void event_cb (flux_t h, flux_msg_handler_t *w,
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
                int type = FLUX_MSGTYPE_ANY;
                const char *topic = "unknown";
                (void)flux_msg_get_type (msg, &type);
                (void)flux_msg_get_topic (msg, &topic);
                flux_log (h, LOG_ERR, "send %s %s to client %.*s: %s",
                          topic, flux_msg_typestr (type),
                          5, zuuid_str (c->uuid),
                          strerror (errno));
                errno = 0;
            }
            count++;
        }
        c = zlist_next (ctx->clients);
    }
    //flux_log (h, LOG_DEBUG, "%s: %s to %d clients", __FUNCTION__, topic, count);
}

static int check_cred (ctx_t *ctx, int fd)
{
    struct ucred ucred;
    socklen_t crlen = sizeof (ucred);
    int rc = -1;

    if (getsockopt (fd, SOL_SOCKET, SO_PEERCRED, &ucred, &crlen) < 0) {
        flux_log_error (ctx->h, "getsockopt SO_PEERCRED");
        goto done;
    }
    assert (crlen == sizeof (ucred));
    if (ucred.uid != ctx->session_owner) {
        flux_log (ctx->h, LOG_ERR, "connect by uid=%d pid=%d denied",
                  ucred.uid, (int)ucred.pid);
        goto done;
    }
    rc = 0;
done:
    return rc;
}

/* Accept a connection from new client.
 */
static void listener_cb (flux_reactor_t *r, flux_watcher_t *w,
                         int revents, void *arg)
{
    int fd = flux_fd_watcher_get_fd (w);
    ctx_t *ctx = arg;
    flux_t h = ctx->h;

    if (revents & FLUX_POLLIN) {
        client_t *c;
        int cfd;

        if ((cfd = accept4 (fd, NULL, NULL, SOCK_CLOEXEC)) < 0) {
            flux_log (h, LOG_ERR, "accept: %s", strerror (errno));
            goto done;
        }
        if (check_cred (ctx, cfd) < 0) {
            close (cfd);
            goto done;
        }
        if (!(c = client_create (ctx, cfd, cfd))) {
            close (cfd);
            goto done;
        }
        if (zlist_append (ctx->clients, c) < 0)
            oom ();
    }
    if (revents & FLUX_POLLERR) {
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

static char *findlocal (const char *globstr)
{
    glob_t globbuf;
    char *uri = NULL;

    if (glob (globstr, GLOB_ONLYDIR, NULL, &globbuf) != 0)
        goto done;
    if (globbuf.gl_pathc < 1) {
        errno = ENOENT;
        goto done;
    }
    if (asprintf (&uri, "local://%s", globbuf.gl_pathv[0]) < 0) {
        errno = ENOMEM;
        goto done;
    }
done:
    return uri;
}

/* Given a JOB identifier that is not a URI, try to turn it into a URI.
 * Returns uri on success (caller must free), or NULL on failure errno set.
 */
static char *findjob (const char *job)
{
    char *tmpdir = getenv ("TMPDIR");
    char *uri = NULL;
    char globstr[PATH_MAX + 1];

    if (!tmpdir)
        tmpdir = "/tmp";
    /* Try to interpret 'job' as the path from local://
     * as we might get from ssh://host/path/to/sockdir
     */
    if (access (job, F_OK) == 0) {
        if (asprintf (&uri, "local://%s", job) < 0) {
            errno = ENOMEM;
            goto done;
        }
        goto done;
    }
    /* Try to interpret 'job' as the flux session-id.
     */
    snprintf (globstr, sizeof (globstr), "%s/flux-%s-*/0", tmpdir, job);
    if ((uri = findlocal (globstr)))
        goto done;
    /* Try to interpret 'job' as "/session-id",
     * as we might get from ssh://host/jobid
     */
    if (strlen (job) > 1 && job[0] == '/' && isdigit (job[1])) {
        snprintf (globstr, sizeof (globstr), "%s/flux-%s-*/0", tmpdir, job + 1);
        if ((uri = findlocal (globstr)))
            goto done;
    }
    /* Not found, return NULL */
    errno = ENOENT;
done:
    return uri;
}

static int child_cb (struct subprocess *p, void *arg)
{
    ctx_t *ctx = arg;

    ctx->exit_code = subprocess_exit_code (p);
    flux_reactor_stop (ctx->reactor);
    subprocess_destroy (p);
    return 0;
}

static int child_create (ctx_t *ctx, int ac, char **av, const char *workpath)
{
    const char *shell = getenv ("SHELL");
    char *argz = NULL;
    size_t argz_len = 0;
    struct subprocess *p = NULL;
    int i;

    if (!shell)
        shell = "/bin/sh";

    for (i = 0; i < ac; i++) {
        if (argz_add (&argz, &argz_len, av[i]) != 0) {
            errno = ENOMEM;
            goto error;
        }
    }
    if (argz)
        argz_stringify (argz, argz_len, ' ');

    if (!(p = subprocess_create (ctx->sm))
            || subprocess_set_callback (p, child_cb, ctx) < 0
            || subprocess_argv_append (p, shell) < 0
            || (argz && subprocess_argv_append (p, "-c") < 0)
            || (argz && subprocess_argv_append (p, argz) < 0)
            || subprocess_set_environ (p, environ) < 0
            || subprocess_setenvf (p, "FLUX_URI", 1,
                                   "local://%s", workpath) < 0
            || subprocess_run (p) < 0)
        goto error;

    if (argz)
        free (argz);
    ctx->p = p;
    return 0;
error:
    if (p)
        subprocess_destroy (p);
    if (argz)
        free (argz);
    return -1;
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,     NULL,          event_cb },
    { FLUX_MSGTYPE_RESPONSE,  NULL,          response_cb },
    FLUX_MSGHANDLER_TABLE_END
};

static struct optparse_option proxy_opts[] = {
    { .name = "stdio", .key = 's', .has_arg = 0,
      .usage = "Present proxy interface on stdio", },
    { .name = "setenv", .key = 'e', .has_arg = 1,
      .usage = "Set NAME=VALUE in flux-proxy environment", },
    OPTPARSE_TABLE_END
};

static int cmd_proxy (optparse_t *p, int ac, char *av[])
{
    flux_t h = NULL;
    int n;
    ctx_t *ctx;
    const char *tmpdir = getenv ("TMPDIR");
    char workpath[PATH_MAX + 1];
    char sockpath[PATH_MAX + 1];
    char pidfile[PATH_MAX + 1];
    const char *job;
    const char *optarg;
    int optind;

    log_init ("flux-proxy");

    optind = optparse_optind (p);
    if (optind == ac)
        optparse_fatal_usage (p, 1, "JOB argument is required\n");
    job = av[optind++];

    if (optparse_hasopt (p, "stdio") && optind != ac)
        optparse_fatal_usage (p, 1, "there can be no COMMAND with --stdio\n");

    while ((optarg = optparse_getopt_next (p, "setenv"))) {
        char *name = xstrdup (optarg);
        char *val = strchr (name, '=');
        if (val)
            *val++ = '\0';
        if (!val)
            optparse_fatal_usage (p, 1, "--setenv optarg format is NAME=VAL");
        setenv (name, val, 1);
        free (name);
    }

    if (strstr (job, "://")) {
        if (!(h = flux_open (job, 0)))
            err_exit ("%s", job);
    } else {
        char *uri = findjob (job);
        if (!uri)
            err_exit ("%s", job);
        if (!(h = flux_open (uri, 0)))
            err_exit ("%s", uri);
         free (uri);
    }

    flux_log_set_facility (h, "proxy");
    ctx = ctx_create (h);

    ctx->listen_fd = -1;

    if (optparse_hasopt (p, "stdio")) {
        client_t *c;
        if (!(c = client_create (ctx, STDIN_FILENO, STDOUT_FILENO)))
            err_exit ("error creating stdio client");
        if (zlist_append (ctx->clients, c) < 0)
            oom ();
        ctx->oneshot = true;
    } else {
        /* Create socket directory.
         */
        n = snprintf (workpath, sizeof (workpath), "%s/flux-proxy-XXXXXX",
                                 tmpdir ? tmpdir : "/tmp");
        assert (n < sizeof (workpath));
        if (!mkdtemp (workpath))
            err_exit ("error creating proxy socket directory");
        cleanup_push_string(cleanup_directory, workpath);

        /* Write proxy pid to broker.pid file.
         * Local connector expects this.
         */
        n = snprintf (pidfile, sizeof (pidfile), "%s/broker.pid", workpath);
        assert (n < sizeof (pidfile));
        FILE *f = fopen (pidfile, "w");
        if (!f || fprintf (f, "%d", getpid ()) < 0 || fclose (f) == EOF)
            err_exit ("%s", pidfile);
        cleanup_push_string(cleanup_file, pidfile);

        /* Listen on socket
         */
        n = snprintf (sockpath, sizeof (sockpath), "%s/local", workpath);
        assert (n < sizeof (sockpath));
        if ((ctx->listen_fd = listener_init (ctx, sockpath)) < 0)
            goto done;
        if (!(ctx->listen_w = flux_fd_watcher_create (ctx->reactor,
                                               ctx->listen_fd,
                                               FLUX_POLLIN | FLUX_POLLERR,
                                               listener_cb, ctx))) {
            goto done;
        }
        flux_watcher_start (ctx->listen_w);

        /* Create child
         */
        if (child_create (ctx, ac - optind, av + optind, workpath) < 0)
            err_exit ("child_create");
   }

    /* Create/start event/response message watchers
     */
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msg_watcher_addvec: %s", strerror (errno));
        goto done;
    }

    /* Start reactor
     */
    if (flux_reactor_run (ctx->reactor, 0) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_run: %s", strerror (errno));
        goto done;
    }
done:
    flux_msg_handler_delvec (htab);
    flux_watcher_destroy (ctx->listen_w);
    if (ctx->listen_fd >= 0) {
        if (close (ctx->listen_fd) < 0)
            flux_log (h, LOG_ERR, "close listen_fd: %s", strerror (errno));
    }
    if (ctx->clients) {
        client_t *c;
        while ((c = zlist_pop (ctx->clients)))
            client_destroy (c);
    }
    if (ctx->exit_code)
        exit (ctx->exit_code);

    ctx_destroy (ctx);
    flux_close (h);
    return (0);
}

int subcommand_proxy_register (optparse_t *p)
{
    optparse_err_t e;

    e = optparse_reg_subcommand (p, "proxy", cmd_proxy,
        "[OPTIONS] JOB [COMMAND...]",
        "Route messages to/from Flux instance",
        proxy_opts);
    if (e != OPTPARSE_SUCCESS)
        return (-1);

    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
