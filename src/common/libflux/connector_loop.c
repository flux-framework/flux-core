/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* loop connector - mainly for testing */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <poll.h>
#include <flux/core.h>

#include "ccan/str/str.h"
#include "msg_deque.h"

struct loop_ctx {
    flux_t *h;
    struct flux_msg_cred cred;
    struct msg_deque *queue;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct loop_ctx *ctx = impl;
    int e, revents = 0;

    if ((e = msg_deque_pollevents (ctx->queue)) < 0)
        return e;
    if (e & POLLIN)
        revents |= FLUX_POLLIN;
    if (e & POLLOUT)
        revents |= FLUX_POLLOUT;
    if (e & POLLERR)
        revents |= FLUX_POLLERR;
    return revents;
}

static int op_pollfd (void *impl)
{
    struct loop_ctx *ctx = impl;
    return msg_deque_pollfd (ctx->queue);
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct loop_ctx *ctx = impl;
    flux_msg_t *cpy;
    struct flux_msg_cred cred;

    if (!(cpy = flux_msg_copy (msg, true)))
        goto error;
    if (flux_msg_get_cred (cpy, &cred) < 0)
        goto error;
    if (cred.userid == FLUX_USERID_UNKNOWN)
        cred.userid = ctx->cred.userid;
    if (cred.rolemask == FLUX_ROLE_NONE)
        cred.rolemask = ctx->cred.rolemask;
    if (flux_msg_set_cred (cpy, cred) < 0)
        goto error;
    if (msg_deque_push_back (ctx->queue, cpy) < 0)
        goto error;
    return 0;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct loop_ctx *ctx = impl;
    flux_msg_t *msg = msg_deque_pop_front (ctx->queue);
    if (!msg)
        errno = EWOULDBLOCK;
    return msg;
}

static int op_getopt (void *impl, const char *option, void *val, size_t size)
{
    struct loop_ctx *ctx = impl;

    if (streq (option, FLUX_OPT_TESTING_USERID)) {
        if (size != sizeof (ctx->cred.userid) || !val)
            goto error;
        memcpy (val, &ctx->cred.userid, size);
    }
    else if (streq (option, FLUX_OPT_TESTING_ROLEMASK)) {
        if (size != sizeof (ctx->cred.rolemask) || !val)
            goto error;
        memcpy (val, &ctx->cred.rolemask, size);
    }
    else if (streq (option, "flux::message_count_limit")) {
        int limit;
        if ((limit = msg_deque_get_limit (ctx->queue)) < 0)
            return -1;
        if (size != sizeof (limit) || !val)
            goto error;
        memcpy (val, &limit, size);
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static int op_setopt (void *impl,
                      const char *option,
                      const void *val,
                      size_t size)
{
    struct loop_ctx *ctx = impl;
    size_t val_size;

    if (streq (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (ctx->cred.userid);
        if (size != val_size || !val)
            goto error;
        memcpy (&ctx->cred.userid, val, val_size);
    }
    else if (streq (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (ctx->cred.rolemask);
        if (size != val_size || !val)
            goto error;
        memcpy (&ctx->cred.rolemask, val, val_size);
    }
    else if (streq (option, "flux::message_count_limit")) {
        int limit;
        val_size = sizeof (limit);
        if (size != val_size || !val)
            goto error;
        memcpy (&limit, val, val_size);
        if (msg_deque_set_limit (ctx->queue, limit) < 0)
            return -1;
    }
    else
        goto error;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

static void op_fini (void *impl)
{
    struct loop_ctx *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        msg_deque_destroy (ctx->queue);
        free (ctx);
        errno = saved_errno;
    }
}

flux_t *connector_loop_init (const char *path, int flags, flux_error_t *errp)
{
    struct loop_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->queue = msg_deque_create (MSG_DEQUE_SINGLE_THREAD)))
        goto error;
    ctx->cred.userid = getuid ();
    ctx->cred.rolemask = FLUX_ROLE_OWNER;
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    /* Fake out size, rank attributes for testing.
     */
    (void)flux_attr_set_cacheonly(ctx->h, "rank", "0");
    (void)flux_attr_set_cacheonly (ctx->h, "size", "1");
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
    .getopt = op_getopt,
    .setopt = op_setopt,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
