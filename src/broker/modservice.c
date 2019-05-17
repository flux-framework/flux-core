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
#    include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>
#include <argz.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"
#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"

#include "module.h"
#include "modservice.h"
#include "ping.h"
#include "rusage.h"

typedef struct {
    flux_t *h;
    module_t *p;
    zlist_t *handlers;
    flux_watcher_t *w_prepare;
    flux_watcher_t *w_check;
} modservice_ctx_t;

static void freectx (void *arg)
{
    modservice_ctx_t *ctx = arg;
    flux_msg_handler_t *mh;
    while ((mh = zlist_pop (ctx->handlers)))
        flux_msg_handler_destroy (mh);
    zlist_destroy (&ctx->handlers);
    flux_watcher_destroy (ctx->w_prepare);
    flux_watcher_destroy (ctx->w_check);
    free (ctx);
}

static modservice_ctx_t *getctx (flux_t *h, module_t *p)
{
    modservice_ctx_t *ctx = flux_aux_get (h, "flux::modservice");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->handlers = zlist_new ()))
            oom ();
        ctx->h = h;
        ctx->p = p;
        flux_aux_set (h, "flux::modservice", ctx, freectx);
    }
    return ctx;
}

static void stats_get_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    flux_msgcounters_t mcs;

    flux_get_msgcounters (h, &mcs);

    if (flux_respond_pack (h,
                           msg,
                           "{ s:i s:i s:i s:i s:i s:i s:i s:i }",
                           "#request (tx)",
                           mcs.request_tx,
                           "#request (rx)",
                           mcs.request_rx,
                           "#response (tx)",
                           mcs.response_tx,
                           "#response (rx)",
                           mcs.response_rx,
                           "#event (tx)",
                           mcs.event_tx,
                           "#event (rx)",
                           mcs.event_rx,
                           "#keepalive (tx)",
                           mcs.keepalive_tx,
                           "#keepalive (rx)",
                           mcs.keepalive_rx)
        < 0)
        FLUX_LOG_ERROR (h);
}

static void stats_clear_event_cb (flux_t *h,
                                  flux_msg_handler_t *mh,
                                  const flux_msg_t *msg,
                                  void *arg)
{
    flux_clr_msgcounters (h);
}

static void stats_clear_request_cb (flux_t *h,
                                    flux_msg_handler_t *mh,
                                    const flux_msg_t *msg,
                                    void *arg)
{
    flux_clr_msgcounters (h);
    if (flux_respond (h, msg, NULL) < 0)
        FLUX_LOG_ERROR (h);
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

    if (flux_request_unpack (msg, NULL, "{s:s s:i}", "op", &op, "flags", &flags)
        < 0)
        goto error;
    if (!(debug_flags = flux_aux_get (h, "flux::debug_flags"))) {
        if (!(debug_flags = calloc (1, sizeof (*debug_flags)))) {
            errno = ENOMEM;
            goto error;
        }
        flux_aux_set (h, "flux::debug_flags", debug_flags, free);
    }
    if (!strcmp (op, "setbit"))
        *debug_flags |= flags;
    else if (!strcmp (op, "clrbit"))
        *debug_flags &= ~flags;
    else if (!strcmp (op, "set"))
        *debug_flags = flags;
    else if (!strcmp (op, "clr"))
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
 */
static void prepare_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    modservice_ctx_t *ctx = arg;
    flux_msg_t *msg = flux_keepalive_encode (0, FLUX_MODSTATE_SLEEPING);
    if (!msg || flux_send (ctx->h, msg, 0) < 0)
        flux_log_error (ctx->h, "error sending keepalive");
    flux_msg_destroy (msg);
}

/* Reactor loop just unblocked.
 */
static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    modservice_ctx_t *ctx = arg;
    flux_msg_t *msg = flux_keepalive_encode (0, FLUX_MODSTATE_RUNNING);
    if (!msg || flux_send (ctx->h, msg, 0) < 0)
        flux_log_error (ctx->h, "error sending keepalive");
    flux_msg_destroy (msg);
}

static void register_event (modservice_ctx_t *ctx,
                            const char *name,
                            flux_msg_handler_f cb)
{
    struct flux_match match = FLUX_MATCH_EVENT;
    flux_msg_handler_t *mh;

    match.topic_glob = xasprintf ("%s.%s", module_get_name (ctx->p), name);
    if (!(mh = flux_msg_handler_create (ctx->h, match, cb, ctx->p)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_start (mh);
    if (zlist_append (ctx->handlers, mh) < 0)
        oom ();
    if (flux_event_subscribe (ctx->h, match.topic_glob) < 0)
        log_err_exit ("%s: flux_event_subscribe %s",
                      __FUNCTION__,
                      match.topic_glob);
    free (match.topic_glob);
}

static void register_request (modservice_ctx_t *ctx,
                              const char *name,
                              flux_msg_handler_f cb,
                              uint32_t rolemask)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    flux_msg_handler_t *mh;

    match.topic_glob = xasprintf ("%s.%s", module_get_name (ctx->p), name);
    if (!(mh = flux_msg_handler_create (ctx->h, match, cb, ctx->p)))
        log_err_exit ("flux_msg_handler_create");
    flux_msg_handler_allow_rolemask (mh, rolemask);
    flux_msg_handler_start (mh);
    if (zlist_append (ctx->handlers, mh) < 0)
        oom ();
    free (match.topic_glob);
}

void modservice_register (flux_t *h, module_t *p)
{
    modservice_ctx_t *ctx = getctx (h, p);
    flux_reactor_t *r = flux_get_reactor (h);

    register_request (ctx, "shutdown", shutdown_cb, FLUX_ROLE_OWNER);
    register_request (ctx, "stats.get", stats_get_cb, FLUX_ROLE_ALL);
    register_request (ctx,
                      "stats.clear",
                      stats_clear_request_cb,
                      FLUX_ROLE_OWNER);
    register_request (ctx, "debug", debug_cb, FLUX_ROLE_OWNER);

    if (ping_initialize (h, module_get_name (ctx->p)) < 0)
        log_err_exit ("ping_initialize");
    if (rusage_initialize (h, module_get_name (ctx->p)) < 0)
        log_err_exit ("rusage_initialize");

    register_event (ctx, "stats.clear", stats_clear_event_cb);

    if (!(ctx->w_prepare = flux_prepare_watcher_create (r, prepare_cb, ctx)))
        log_err_exit ("flux_prepare_watcher_create");
    if (!(ctx->w_check = flux_check_watcher_create (r, check_cb, ctx)))
        log_err_exit ("flux_check_watcher_create");
    flux_watcher_start (ctx->w_prepare);
    flux_watcher_start (ctx->w_check);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
