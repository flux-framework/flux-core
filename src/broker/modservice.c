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
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "ccan/str/str.h"

#include "module.h"
#include "modservice.h"
#include "method.h"

typedef struct {
    flux_t *h;
    flux_watcher_t *w_prepare;
    flux_msg_handler_t **handlers_default;
    flux_msg_handler_t **handlers;
} modservice_ctx_t;

static void freectx (void *arg)
{
    modservice_ctx_t *ctx = arg;
    flux_msg_handler_delvec (ctx->handlers);
    flux_msg_handler_delvec (ctx->handlers_default);
    flux_watcher_destroy (ctx->w_prepare);
    free (ctx);
}

static modservice_ctx_t *getctx (flux_t *h)
{
    modservice_ctx_t *ctx = flux_aux_get (h, "flux::modservice");

    if (!ctx) {
        if (!(ctx = calloc (1, sizeof (*ctx))))
            return NULL;
        ctx->h = h;
        flux_aux_set (h, "flux::modservice", ctx, freectx);
    }
    return ctx;
}

static void shutdown_cb (flux_t *h,
                         flux_msg_handler_t *mh,
                         const flux_msg_t *msg,
                         void *arg)
{
    flux_reactor_stop (flux_get_reactor (h));
}

/* Reactor loop is about to block.
 * Notify broker that module is running, then disable the prepare watcher.
 */
static void prepare_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    modservice_ctx_t *ctx = arg;

    if (flux_module_set_running (ctx->h) < 0)
        flux_log_error (ctx->h, "error setting module status to running");
    flux_watcher_destroy (ctx->w_prepare);
    ctx->w_prepare = NULL;
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "shutdown",
      shutdown_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static int mod_subscribe (flux_t *h, const char *name, const char *method)
{
    char *topic;

    if (asprintf (&topic, "%s.%s", name, method) < 0
        || flux_event_subscribe (h, topic) < 0) {
        ERRNO_SAFE_WRAP (free, topic);
        return -1;
    }
    free (topic);
    return 0;
}

int modservice_register (flux_t *h)
{
    modservice_ctx_t *ctx = getctx (h);
    flux_reactor_t *r = flux_get_reactor (h);
    const char *name = flux_aux_get (h, "flux::name");

    if (!ctx || !r)
        return -1;

    if (flux_register_default_methods (h, name, &ctx->handlers_default) < 0
        || flux_msg_handler_addvec_ex (h, name, htab, ctx, &ctx->handlers) < 0)
        return -1;

    if (mod_subscribe (h, name, "stats-clear") < 0)
        return -1;

    if (!(ctx->w_prepare = flux_prepare_watcher_create (r, prepare_cb, ctx)))
        return -1;

    flux_watcher_start (ctx->w_prepare);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
