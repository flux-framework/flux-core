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
#include <unistd.h>
#include <flux/core.h>

#include "src/common/librouter/usock.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

struct local_connector {
    struct usock_client *uclient;
    uint32_t testing_userid;
    uint32_t testing_rolemask;
    uint32_t owner;
    flux_t *h;
    int fd;
    char *path;
};

static const struct flux_handle_ops handle_ops;

static void local_disconnect (struct local_connector *ctx);
static int local_connect (struct local_connector *ctx);

static int op_pollevents (void *impl)
{
    struct local_connector *ctx = impl;

    return ctx->uclient ? usock_client_pollevents (ctx->uclient) : 0;
}

static int op_pollfd (void *impl)
{
    struct local_connector *ctx = impl;

    return ctx->uclient ? usock_client_pollfd (ctx->uclient) : -1;
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

static int op_setopt (void *impl,
                      const char *option,
                      const void *val,
                      size_t size)
{
    struct local_connector *ctx = impl;
    size_t val_size;
    int rc = -1;

    if (streq (option, FLUX_OPT_TESTING_USERID)) {
        val_size = sizeof (ctx->testing_userid);
        if (size != val_size || !val) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_userid, val, val_size);
    }
    else if (streq (option, FLUX_OPT_TESTING_ROLEMASK)) {
        val_size = sizeof (ctx->testing_rolemask);
        if (size != val_size || !val) {
            errno = EINVAL;
            goto done;
        }
        memcpy (&ctx->testing_rolemask, val, val_size);
    }
    else {
        errno = EINVAL;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

static int op_getopt (void *impl,
                      const char *option,
                      void *val,
                      size_t size)
{
    struct local_connector *ctx = impl;
    size_t val_size;
    int rc = -1;

    if (streq (option, "flux::owner")) {
        val_size = sizeof (ctx->owner);
        if (size != val_size || !val) {
            errno = EINVAL;
            goto done;
        }
        memcpy (val, &ctx->owner, val_size);
    }
    else {
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
        local_disconnect (ctx);
        free (ctx->path);
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

flux_t *connector_local_init (const char *path, int flags, flux_error_t *errp)
{
    struct local_connector *ctx;

    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;

    ctx->testing_userid = FLUX_USERID_UNKNOWN;
    ctx->testing_rolemask = FLUX_ROLE_NONE;
    ctx->owner = FLUX_USERID_UNKNOWN;
    ctx->fd = -1;

    if (!(ctx->path = strdup (path)))
        goto error;
    if (local_connect (ctx) < 0) {
        if (errno == ENOENT)
            errprintf (errp,
                       "broker socket %s was not found",
                       path);
        goto error;
    }
    if (!(ctx->h = flux_handle_create (ctx, &handle_ops, flags)))
        goto error;
    return ctx->h;
error:
    op_fini (ctx);
    return NULL;
}

static void local_disconnect (struct local_connector *ctx)
{
    if (ctx->uclient) {
        usock_client_destroy (ctx->uclient);
        ctx->uclient = NULL;
    }
    if (ctx->fd >= 0) {
        close (ctx->fd);
        ctx->fd = -1;
    }
    ctx->owner = FLUX_USERID_UNKNOWN;
}

static int local_connect (struct local_connector *ctx)
{
    struct usock_retry_params retry = USOCK_RETRY_DEFAULT;
    struct flux_msg_cred server_cred;

    if (override_retry_count (&retry) < 0
        || (ctx->fd = usock_client_connect (ctx->path, retry)) < 0
        || usock_get_cred (ctx->fd, &server_cred) < 0
        || !(ctx->uclient = usock_client_create (ctx->fd)))
        return -1;
    ctx->owner = server_cred.userid;
    return 0;
}

static int op_reconnect (void *impl)
{
    struct local_connector *ctx = impl;

    local_disconnect (ctx);
    return local_connect (ctx);
}

static const struct flux_handle_ops handle_ops = {
    .pollfd = op_pollfd,
    .pollevents = op_pollevents,
    .send = op_send,
    .recv = op_recv,
    .reconnect = op_reconnect,
    .setopt = op_setopt,
    .getopt = op_getopt,
    .impl_destroy = op_fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
