/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rlocal.c - specialized resilient variant of the local connector
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/epoll.h>
#include <poll.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/librouter/usock.h"
#include "src/common/librouter/rpc_track.h"

struct rlocal {
    char *path;
    struct usock_client *uclient;
    uint32_t owner;
    flux_t *h;
    int fd;
    int pollfd;
    int flags;
    int connect_count;
    struct rpc_track *tracker;
    int tracker_count;
};

static int rlocal_reconnect (struct rlocal *ctx, int errnum);
static void rlocal_disconnect (struct rlocal *ctx, int errnum);
static int rlocal_connect (struct rlocal *ctx);

static const struct flux_handle_ops handle_ops;

/* contribute to handle trace, if requested */
static void __attribute__ ((format (printf, 2, 3)))
ctrace (struct rlocal *ctx, const char *fmt, ...)
{
    if ((ctx->flags & FLUX_O_TRACE)) {
        va_list ap;
        char buf[256];
        int saved_errno = errno;

        va_start (ap, fmt);
        vsnprintf (buf, sizeof (buf), fmt, ap);
        va_end (ap);

        fprintf (stderr, "--------------------------------------\n");
        fprintf (stderr, "c %s\n", buf);

        errno = saved_errno;
    }
}

/* POLLERR | POLLHUP are translated to POLLIN so that user will call op->recv()
 * and trigger a reconnect.
 */
static int op_pollevents (void *impl)
{
    struct rlocal *ctx = impl;
    struct pollfd pfd;
    int events = 0;

    pfd.fd = ctx->pollfd;
    pfd.events = POLLIN | POLLOUT | POLLERR | POLLHUP;
    pfd.revents = 0;

    if (poll (&pfd, 1, 0) < 0)
        return FLUX_POLLERR;

    if ((pfd.revents & (POLLIN | POLLERR | POLLHUP)))
        events |= FLUX_POLLIN;
    if ((pfd.revents & POLLOUT))
        events |= FLUX_POLLOUT;

    return events;
}

/* In this connector, ctx->fd can change on a reconnect.  To avoid invalidating
 * reactor watchers set up using flux_pollfd(), the internal ctx->fd is wrapped
 * in an epoll fd which remains constant across a reconnect.
 */
static int op_pollfd (void *impl)
{
    struct rlocal *ctx = impl;

    return ctx->pollfd;
}

static void update_tracker (struct rlocal *ctx, const flux_msg_t *msg)
{
    rpc_track_update (ctx->tracker, msg);

    if ((ctx->flags & FLUX_O_TRACE)) {
        int count = rpc_track_count (ctx->tracker);
        if (count != ctx->tracker_count) {
            ctx->tracker_count = count;
            ctrace (ctx, "tracking %d rpcs", ctx->tracker_count);
        }
    }
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct rlocal *ctx = impl;

    while (usock_client_send (ctx->uclient, msg, flags) < 0) {
        if (rlocal_reconnect (ctx, errno) < 0)
            return -1;
    }
    update_tracker (ctx, msg);
    return 0;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct rlocal *ctx = impl;
    flux_msg_t *msg;

    while (!(msg = usock_client_recv (ctx->uclient, flags))) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return NULL;
        /* Expected: ECONNRESET ... */
        if (rlocal_reconnect (ctx, errno) < 0)
            return NULL;
    }
    update_tracker (ctx, msg);
    return msg;
}

static void fail_tracked_request (const flux_msg_t *msg, void *arg)
{
    struct rlocal *ctx = arg;
    flux_msg_t *rep;
    const char *topic = "NULL";
    struct flux_msg_cred lies = { .userid = 0, .rolemask = FLUX_ROLE_OWNER };

    flux_msg_get_topic (msg, &topic);
    if (!(rep = flux_response_derive (msg, ECONNRESET))
        || flux_msg_set_string (rep, "RPC aborted due to broker reconnect") < 0
        || flux_msg_set_cred (rep, lies) < 0
        || flux_requeue (ctx->h, rep, FLUX_RQ_TAIL) < 0) {
        ctrace (ctx,
                "error responding to tracked rpc topic=%s: %s",
                topic,
                strerror (errno));
    }
    else
        ctrace (ctx, "responded to tracked rpc topic=%s", topic);
    flux_msg_destroy (rep);
}

static int op_setopt (void *impl, const char *option,
                      const void *val, size_t size)
{
    errno = EINVAL;
    return -1;
}

static int op_getopt (void *impl, const char *option,
                      void *val, size_t size)
{
    struct rlocal *ctx = impl;
    size_t val_size;
    int rc = -1;

    /* See "Security note" in flux_job_submit() implementation.
     * If implemented, this option optimizes instance owner job submission.
     */
    if (option && !strcmp (option, "flux::owner")) {
        val_size = sizeof (ctx->owner);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (val, &ctx->owner, val_size);
    } else {
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    return rc;
}


static void rlocal_disconnect (struct rlocal *ctx, int errnum)
{
    ctrace (ctx, "disconnect fd %d %s%s",
            ctx->fd,
            errnum > 0 ? "due to " : "",
            errnum > 0 ? strerror (errnum) : "");
    if (ctx->uclient) {
        usock_client_destroy (ctx->uclient);
        ctx->uclient = NULL;
    }
    if (ctx->fd >= 0) {
        if (ctx->pollfd >= 0) {
            struct epoll_event ev; // ignored but needed for kernel < 2.6.9
            epoll_ctl (ctx->pollfd, EPOLL_CTL_DEL, ctx->fd, &ev);
        }
        close (ctx->fd);
        ctx->fd = -1;
    }
    ctx->owner = FLUX_USERID_UNKNOWN;
}

static int rlocal_connect (struct rlocal *ctx)
{
    struct flux_msg_cred server_cred;
    struct epoll_event ev;

    ctrace (ctx, "connecting %s", ctx->path);
    ctx->fd = usock_client_connect (ctx->path, USOCK_RETRY_FOREVER);
    if (ctx->fd < 0) {
        ctrace (ctx, "connect %s failed: %s", ctx->path, strerror (errno));
        goto error;
    }
    if (usock_get_cred (ctx->fd, &server_cred) < 0) {
        ctrace (ctx, "get peer cred failed: %s", strerror (errno));
        goto error;
    }
    ctx->owner = server_cred.userid;
    if (!(ctx->uclient = usock_client_create (ctx->fd))) {
        ctrace (ctx, "create usock client failed: %s", strerror (errno));
        goto error;
    }
    ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP;
    ev.data.fd = ctx->fd;
    if (epoll_ctl (ctx->pollfd, EPOLL_CTL_ADD, ctx->fd, &ev) < 0)
        goto error;
    ctrace (ctx, "connected %s owner %d fd %d reconnects %d",
            ctx->path,
            (int)ctx->owner,
            ctx->fd,
            ctx->connect_count);
    ctx->connect_count++;
    return 0;
error:
    rlocal_disconnect (ctx, errno);
    return -1;
}

static int rlocal_reconnect (struct rlocal *ctx, int errnum)
{
    rlocal_disconnect (ctx, errnum);
    if ((ctx->flags & FLUX_O_TRACE))
        ctrace (ctx, "purging %d rpcs", ctx->tracker_count);
    if (rlocal_connect (ctx) < 0)
        return -1;
    rpc_track_purge (ctx->tracker, fail_tracked_request, ctx);
    return 0;
}

static void op_fini (void *impl)
{
    struct rlocal *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        rlocal_disconnect (ctx, 0);
        if (ctx->pollfd >= 0)
            close (ctx->pollfd);
        rpc_track_destroy (ctx->tracker);
        free (ctx->path);
        free (ctx);
        errno = saved_errno;
    }
}

flux_t *connector_init (const char *path, int flags)
{
    struct rlocal *ctx;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->owner = FLUX_USERID_UNKNOWN;
    ctx->fd = -1;
    ctx->pollfd = -1;
    ctx->flags = flags;
    if ((ctx->pollfd = epoll_create1 (EPOLL_CLOEXEC)) < 0)
        goto error;
    if (!(ctx->path = strdup (path)))
        goto error;
    if (rlocal_connect (ctx) < 0)
        goto error;
    if (!(ctx->tracker = rpc_track_create (MSG_HASH_TYPE_UUID_MATCHTAG)))
        goto error;
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    return ctx->h;
error:
    op_fini (ctx);
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .setopt = op_setopt,
    .getopt = op_getopt,
    .impl_destroy = op_fini,
};

// vi:ts=4 sw=4 expandtab
