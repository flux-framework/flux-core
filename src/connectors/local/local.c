/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <signal.h>
#include <poll.h>
#include <unistd.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/macros.h"
#include "src/common/libutil/fdutils.h"

#define CTX_MAGIC 0xf434aaab
typedef struct {
    int magic;
    int fd;
    int fd_nonblock;
    struct flux_msg_iobuf outbuf;
    struct flux_msg_iobuf inbuf;
    uint32_t testing_userid;
    uint32_t testing_rolemask;
    flux_t *h;
} local_ctx_t;

static const struct flux_handle_ops handle_ops;

static int set_nonblock (local_ctx_t *c, int nonblock)
{
    if (c->fd_nonblock == nonblock)
        return 0;
    if ((nonblock ? fd_set_nonblocking (c->fd) : fd_set_blocking (c->fd)) < 0)
        return -1;
    c->fd_nonblock = nonblock;
    return 0;
}

static int op_pollevents (void *impl)
{
    local_ctx_t *c = impl;
    struct pollfd pfd = {
        .fd = c->fd,
        .events = POLLIN | POLLOUT | POLLERR | POLLHUP,
        .revents = 0,
    };
    int revents = 0;
    switch (poll (&pfd, 1, 0)) {
        case 1:
            if (pfd.revents & POLLIN)
                revents |= FLUX_POLLIN;
            if (pfd.revents & POLLOUT)
                revents |= FLUX_POLLOUT;
            if ((pfd.revents & POLLERR) || (pfd.revents & POLLHUP))
                revents |= FLUX_POLLERR;
            break;
        case 0:
            break;
        default: /* -1 */
            revents |= FLUX_POLLERR;
            break;
    }
    return revents;
}

static int op_pollfd (void *impl)
{
    local_ctx_t *c = impl;
    return c->fd;
}

static int send_normal (local_ctx_t *c, const flux_msg_t *msg, int flags)
{
    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return -1;
    if (flux_msg_sendfd (c->fd, msg, &c->outbuf) < 0)
        return -1;
    return 0;
}

static int send_testing (local_ctx_t *c, const flux_msg_t *msg, int flags)
{
    flux_msg_t *cpy;
    int rc = -1;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto done;
    if (flux_msg_set_userid (cpy, c->testing_userid) < 0)
        goto done;
    if (flux_msg_set_rolemask (cpy, c->testing_rolemask) < 0)
        goto done;
    rc = send_normal (c, cpy, flags);
done:
    flux_msg_destroy (cpy);
    return rc;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);
    if (c->testing_userid != FLUX_USERID_UNKNOWN
        || c->testing_rolemask != FLUX_ROLE_NONE)
        return send_testing (c, msg, flags);
    else
        return send_normal (c, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    if (set_nonblock (c, (flags & FLUX_O_NONBLOCK)) < 0)
        return NULL;
    return flux_msg_recvfd (c->fd, &c->inbuf);
}

static int op_event (void *impl, const char *topic, const char *msg_topic)
{
    local_ctx_t *c = impl;
    flux_future_t *f;
    int rc = -1;

    assert (c->magic == CTX_MAGIC);

    if (!(f = flux_rpc_pack (c->h,
                             msg_topic,
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "topic",
                             topic)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int op_event_subscribe (void *impl, const char *topic)
{
    return op_event (impl, topic, "local.sub");
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    return op_event (impl, topic, "local.unsub");
}

static int op_setopt (void *impl, const char *option, const void *val, size_t size)
{
    local_ctx_t *ctx = impl;
    assert (ctx->magic == CTX_MAGIC);
    size_t val_size;
    int rc = -1;

    if (option && !strcmp (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (ctx->testing_userid);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_userid, val, val_size);
    } else if (option && !strcmp (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (ctx->testing_rolemask);
        if (size != val_size) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_rolemask, val, val_size);
    } else {
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static void op_fini (void *impl)
{
    local_ctx_t *c = impl;
    assert (c->magic == CTX_MAGIC);

    flux_msg_iobuf_clean (&c->outbuf);
    flux_msg_iobuf_clean (&c->inbuf);
    if (c->fd >= 0)
        (void)close (c->fd);
    c->magic = ~CTX_MAGIC;
    free (c);
}

static int env_getint (char *name, int dflt)
{
    char *s = getenv (name);
    return s ? strtol (s, NULL, 10) : dflt;
}

/* Connect socket `fd` to unix domain socket `file` and fail after `retries`
 *  attempts with exponential retry backoff starting at 16ms.
 * Return 0 on success, or -1 on failure.
 */
static int connect_sock_with_retry (int fd, const char *file, int retries)
{
    int count = 0;
    struct sockaddr_un addr;
    useconds_t s = 8 * 1000;
    int maxdelay = 2000000;
    do {
        memset (&addr, 0, sizeof (struct sockaddr_un));
        addr.sun_family = AF_UNIX;
        if (strncpy (addr.sun_path, file, sizeof (addr.sun_path) - 1) < 0) {
            errno = EINVAL;
            return -1;
        }
        if (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) == 0)
            return 0;
        if (s < maxdelay)
            s = 2 * s < maxdelay ? 2 * s : maxdelay;
    } while ((++count <= retries) && (usleep (s) == 0));
    return -1;
}

/* Path is interpreted as the directory containing the unix domain socket.
 */
flux_t *connector_init (const char *path, int flags)
{
    local_ctx_t *c = NULL;
    char sockfile[SIZEOF_FIELD (struct sockaddr_un, sun_path)];
    int n;
    int retries = env_getint ("FLUX_LOCAL_CONNECTOR_RETRY_COUNT", 5);

    if (!path) {
        errno = EINVAL;
        goto error;
    }
    n = snprintf (sockfile, sizeof (sockfile), "%s/local", path);
    if (n >= sizeof (sockfile)) {
        errno = EINVAL;
        goto error;
    }
    if (!(c = malloc (sizeof (*c)))) {
        errno = ENOMEM;
        goto error;
    }
    memset (c, 0, sizeof (*c));
    c->magic = CTX_MAGIC;

    c->testing_userid = FLUX_USERID_UNKNOWN;
    c->testing_rolemask = FLUX_ROLE_NONE;

    c->fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (c->fd < 0)
        goto error;
    c->fd_nonblock = -1;
    if (connect_sock_with_retry (c->fd, sockfile, retries) < 0)
        goto error;
    /* read 1 byte indicating success or failure of auth */
    unsigned char e;
    int rc;
    rc = read (c->fd, &e, 1);
    if (rc < 0)
        goto error;
    if (rc == 0) {
        errno = ECONNRESET;
        goto error;
    }
    if (e != 0) {
        errno = e;
        goto error;
    }
    flux_msg_iobuf_init (&c->outbuf);
    flux_msg_iobuf_init (&c->inbuf);
    if (!(c->h = flux_handle_create (c, &handle_ops, flags)))
        goto error;
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
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .setopt = op_setopt,
    .getopt = NULL,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
