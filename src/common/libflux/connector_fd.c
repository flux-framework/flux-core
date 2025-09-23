/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* fd connector - use pre-connected file descriptor fd://N
 *
 * The file descriptor is closed on flux_close(), even though the
 * connector didn't open it.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <poll.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/librouter/usock.h"
#include "ccan/str/str.h"

struct fd_ctx {
    int fd;
    struct usock_client *uclient;
    flux_t *h;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct fd_ctx *ctx = impl;

    return ctx->uclient ? usock_client_pollevents (ctx->uclient) : 0;
}

static int op_pollfd (void *impl)
{
    struct fd_ctx *ctx = impl;

    return ctx->uclient ? usock_client_pollfd (ctx->uclient) : -1;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct fd_ctx *ctx = impl;

    return usock_client_send (ctx->uclient, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct fd_ctx *ctx = impl;

    return usock_client_recv (ctx->uclient, flags);
}

static void op_fini (void *impl)
{
    struct fd_ctx *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        usock_client_destroy (ctx->uclient);
        if (ctx->fd >= 0)
            (void)close (ctx->fd);
        free (ctx);
        errno = saved_errno;
    }
}

flux_t *connector_fd_init (const char *path, int flags, flux_error_t *errp)
{
    struct fd_ctx *ctx;
    char *endptr;
    int fd;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->fd = -1;
    if (!path) {
        errprintf (errp, "URI path is not set");
        goto inval;
    }
    errno = 0;
    fd = strtol (path, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || fd < 0) {
        errprintf (errp, "error parsing file descriptor");
        goto inval;
    }
    if (!(ctx->uclient = usock_client_create (fd)))
        goto error;
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    ctx->fd = fd;
    return ctx->h;
inval:
    errno = EINVAL;
error:
    op_fini (ctx);
    return NULL;
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
