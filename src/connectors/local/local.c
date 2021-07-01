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
#include "src/common/libutil/errno_safe.h"
#include "src/common/librouter/usock.h"

struct local_connector {
    struct usock_client *uclient;
    uint32_t testing_userid;
    uint32_t testing_rolemask;
    uint32_t owner;
    flux_t *h;
    int fd;
};

static const struct flux_handle_ops handle_ops;

static int op_pollevents (void *impl)
{
    struct local_connector *ctx = impl;

    return usock_client_pollevents (ctx->uclient);
}

static int op_pollfd (void *impl)
{
    struct local_connector *ctx = impl;

    return usock_client_pollfd (ctx->uclient);
}

/* Special send function for testing that sets the userid/rolemask to
 * values set with flux_opt_set().  The connector-local module
 * overwrites these credentials for guests, but allows pass-through for
 * instance owner.  This is useful for service access control testing.
 */
static int send_testing (struct local_connector *ctx,
                         const flux_msg_t *msg,
                         int flags)
{
    flux_msg_t *cpy;

    if (!(cpy = flux_msg_copy (msg, true)))
        return -1;
    if (flux_msg_set_userid (cpy, ctx->testing_userid) < 0)
        goto error;
    if (flux_msg_set_rolemask (cpy, ctx->testing_rolemask) < 0)
        goto error;
    if (usock_client_send (ctx->uclient, cpy, flags) < 0)
        goto error;
    flux_msg_destroy (cpy);
    return 0;
error:
    flux_msg_destroy (cpy);
    return -1;
}

static int op_send (void *impl, const flux_msg_t *msg, int flags)
{
    struct local_connector *ctx = impl;

    if (ctx->testing_userid != FLUX_USERID_UNKNOWN
                                || ctx->testing_rolemask != FLUX_ROLE_NONE)
        return send_testing (ctx, msg, flags);

    return usock_client_send (ctx->uclient, msg, flags);
}

static flux_msg_t *op_recv (void *impl, int flags)
{
    struct local_connector *ctx = impl;

    return usock_client_recv (ctx->uclient, flags);
}

static int op_event_subscribe (void *impl, const char *topic)
{
    struct local_connector *ctx = impl;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (ctx->h,
                             "local.sub",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "topic", topic)))
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static int op_event_unsubscribe (void *impl, const char *topic)
{
    struct local_connector *ctx = impl;
    flux_future_t *f;

    if (!(f = flux_rpc_pack (ctx->h,
                             "local.unsub",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "topic", topic)))
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

static int op_setopt (void *impl, const char *option,
                      const void *val, size_t size)
{
    struct local_connector *ctx = impl;
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

static int op_getopt (void *impl, const char *option,
                      void *val, size_t size)
{
    struct local_connector *ctx = impl;
    size_t val_size;
    int rc = -1;

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

static void op_fini (void *impl)
{
    struct local_connector *ctx = impl;

    if (ctx) {
        int saved_errno = errno;
        usock_client_destroy (ctx->uclient);
        if (ctx->fd >= 0)
            ERRNO_SAFE_WRAP (close, ctx->fd);
        free (ctx);
        errno = saved_errno;
    }
}

static int override_retry_count (struct usock_retry_params *retry)
{
    const char *s;

    if ((s = getenv ("FLUX_LOCAL_CONNECTOR_RETRY_COUNT"))) {
        char *endptr;
        int n;

        errno = 0;
        n = strtol (s, &endptr, 10);
        if (errno != 0 || *endptr != '\0') {
            errno = EINVAL;
            return  -1;
        }
        retry->max_retry = n;
    }
    return 0;
}

/* Path is interpreted as the directory containing the unix domain socket.
 */
flux_t *connector_init (const char *path, int flags)
{
    struct local_connector *ctx;
    struct usock_retry_params retry = USOCK_RETRY_DEFAULT;
    struct flux_msg_cred server_cred;

    if (!path || override_retry_count (&retry) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;

    ctx->testing_userid = FLUX_USERID_UNKNOWN;
    ctx->testing_rolemask = FLUX_ROLE_NONE;
    ctx->owner = FLUX_USERID_UNKNOWN;

    ctx->fd = usock_client_connect (path, retry);
    if (ctx->fd < 0)
        goto error;
    if (usock_get_cred (ctx->fd, &server_cred) < 0)
        goto error;
    ctx->owner = server_cred.userid;
    if (!(ctx->uclient = usock_client_create (ctx->fd)))
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
    .event_subscribe = op_event_subscribe,
    .event_unsubscribe = op_event_unsubscribe,
    .setopt = op_setopt,
    .getopt = op_getopt,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
