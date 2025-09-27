/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rexec.c - broker subprocess server
 *
 * The service is restricted to the instance owner.
 * In addition, remote access to rank 0 is prohibited on multi-user instances.
 * This is a precaution for system instances where rank 0 is deployed on a
 * management node with restricted user access.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>
#include <flux/core.h>

#include "src/broker/module.h"

#include "src/common/libsubprocess/server.h"
#include "src/common/libutil/errprintf.h"

struct rexec_ctx {
    flux_msg_handler_t **handlers;
    subprocess_server_t *ss;
    flux_t *h;
    flux_future_t *f_shutdown;
};

static const double shutdown_timeout = 5.0;

/* The motivating use case for this was discussed in
 * flux-framework/flux-core#5676
 */
static int is_multiuser_instance (flux_t *h)
{
    int allow_guest_user = 0;
    (void)flux_conf_unpack (flux_get_conf (h),
                            NULL,
                            "{s:{s:b}}",
                            "access",
                              "allow-guest-user", &allow_guest_user);
    return allow_guest_user;
}

static int reject_nonlocal (const flux_msg_t *msg,
                            void *arg,
                            flux_error_t *error)
{
    struct rexec_ctx *ctx = arg;
    if (!flux_msg_is_local (msg) && is_multiuser_instance (ctx->h)) {
        errprintf (error,
               "Remote rexec requests are not allowed on rank 0");
        return -1;
    }
    return 0;
}

static void shutdown_continuation (flux_future_t *f, void *arg)
{
    struct rexec_ctx *ctx = arg;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (flux_future_get (f, NULL) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "subprocess server shutdown: %s",
                  future_strerror (f, errno));
        flux_reactor_stop_error (r);
    }
    else
        flux_reactor_stop (r);
}

/* Override built-in shutdown handler that calls flux_reactor_stop().
 * Send a SIGTERM to all procs.  shutdown_continuation is called after a
 * timeout or when all subprocesses are cleaned up.
 */
static void shutdown_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    struct rexec_ctx *ctx = arg;

    if (ctx->f_shutdown)
        return;
    if (!(ctx->f_shutdown = subprocess_server_shutdown (ctx->ss, SIGTERM))
        || flux_future_then (ctx->f_shutdown,
                             shutdown_timeout,
                             shutdown_continuation,
                             ctx) < 0) {
        flux_log_error (h, "subprocess server shutdown");
        flux_reactor_stop_error (flux_get_reactor (h));
    }
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    const char *errstr = NULL;
    const flux_conf_t *conf;

    if (flux_conf_reload_decode (msg, &conf) < 0) {
        errstr = "Failed to parse config-reload request";
        goto error;
    }
    if (flux_set_conf (h, flux_conf_incref (conf)) < 0) {
        flux_conf_decref (conf);
        errstr = "Failed to update config";
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "config-reload", config_reload_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,  "shutdown", shutdown_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END,
};

static int mod_main (flux_t *h, int argc, char *argv[])
{
    const char *name = flux_aux_get (h, "flux::name");
    struct rexec_ctx ctx = { 0 };
    const char *local_uri;
    uint32_t rank;
    int rc = -1;

    ctx.h = h;
    if (!(local_uri = flux_attr_get (h, "local-uri"))) {
        flux_log_error (h, "error fetching local-uri attribute");
        return -1;
    }
    if (flux_get_rank (h, &rank) < 0) {
        flux_log_error (h, "error fetching rank attribute");
        return -1;
    }
    if (!(ctx.ss = subprocess_server_create (h,
                                             name,
                                             local_uri,
                                             flux_llog,
                                             h))) {
        // logs errors via flux_llog()
        return -1;
    }
    if (rank == 0)
        subprocess_server_set_auth_cb (ctx.ss, reject_nonlocal, &ctx);
    if (flux_msg_handler_addvec_ex (h, name, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "error registering message handlers");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_future_destroy (ctx.f_shutdown);
    flux_msg_handler_delvec (ctx.handlers);
    subprocess_server_destroy (ctx.ss);
    return rc;
}

struct module_builtin builtin_rexec= {
    .name = "rexec",
    .main = mod_main,
};


// vi:ts=4 sw=4 expandtab
