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
#include "src/common/libfluxutil/method.h"
#include "ccan/str/str.h"

#include "module.h"
#include "modservice.h"

typedef struct {
    flux_t *h;
    flux_watcher_t *w_prepare;
    flux_msg_handler_t **handlers;
} modservice_ctx_t;

static void freectx (void *arg)
{
    modservice_ctx_t *ctx = arg;
    flux_msg_handler_delvec (ctx->handlers);
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

static void debug_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    int flags;
    int *debug_flags;
    const char *op;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i}",
                             "op", &op,
                             "flags", &flags) < 0)
        goto error;
    if (!(debug_flags = flux_aux_get (h, "flux::debug_flags"))) {
        if (!(debug_flags = calloc (1, sizeof (*debug_flags))))
            goto error;
        flux_aux_set (h, "flux::debug_flags", debug_flags, free);
    }
    if (streq (op, "setbit"))
        *debug_flags |= flags;
    else if (streq (op, "clrbit"))
        *debug_flags &= ~flags;
    else if (streq (op, "set"))
        *debug_flags = flags;
    else if (streq (op, "clr"))
        *debug_flags = 0;
    else {
        errno = EPROTO;
        goto error;
    }
    if (flux_respond_pack (h, msg, "{s:i}", "flags", *debug_flags) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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
    { FLUX_MSGTYPE_REQUEST,
      "stats-get",
      method_stats_get_cb,
      FLUX_ROLE_ALL,
    },
    { FLUX_MSGTYPE_REQUEST,
      "stats-clear",
      method_stats_clear_cb,
      0,
    },
    { FLUX_MSGTYPE_EVENT,
      "stats-clear",
      method_stats_clear_event_cb,
      0,
    },
    { FLUX_MSGTYPE_REQUEST,
      "debug",
      debug_cb,
      0,
    },
    { FLUX_MSGTYPE_REQUEST,
      "rusage",
      method_rusage_cb,
      FLUX_ROLE_USER,
    },
    { FLUX_MSGTYPE_REQUEST,
      "ping",
      method_ping_cb,
      FLUX_ROLE_USER,
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

    if (flux_msg_handler_addvec_ex (h, name, htab, ctx, &ctx->handlers) < 0)
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
