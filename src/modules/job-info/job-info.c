/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <czmq.h>
#include <flux/core.h>

#include "info.h"
#include "allow.h"
#include "job_state.h"
#include "list.h"
#include "lookup.h"
#include "watch.h"
#include "guest_watch.h"
#include "idsync.h"

static void disconnect_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    char *sender;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: flux_request_decode", __FUNCTION__);
        return;
    }
    if (flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        return;
    }
    watchers_cancel (ctx, sender, FLUX_MATCHTAG_NONE);
    guest_watchers_cancel (ctx, sender, FLUX_MATCHTAG_NONE);
    free (sender);
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    int lookups = zlist_size (ctx->lookups);
    int watchers = zlist_size (ctx->watchers);
    int guest_watchers = zlist_size (ctx->guest_watchers);
    int pending = zlistx_size (ctx->jsctx->pending);
    int running = zlistx_size (ctx->jsctx->running);
    int inactive = zlistx_size (ctx->jsctx->inactive);
    int idsync_lookups = zlistx_size (ctx->idsync_lookups);
    int idsync_waits = zhashx_size (ctx->idsync_waits);
    if (flux_respond_pack (h, msg, "{s:i s:i s:i s:{s:i s:i s:i} s:{s:i s:i}}",
                           "lookups", lookups,
                           "watchers", watchers,
                           "guest_watchers", guest_watchers,
                           "jobs",
                           "pending", pending,
                           "running", running,
                           "inactive", inactive,
                           "idsync",
                           "lookups", idsync_lookups,
                           "waits", idsync_waits) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void job_stats_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    int total = ctx->jsctx->depend_count + ctx->jsctx->sched_count +
        ctx->jsctx->run_count + ctx->jsctx->cleanup_count +
        ctx->jsctx->inactive_count;
    if (flux_respond_pack (h,
                           msg,
                           "{s:{s:i s:i s:i s:i s:i s:i}}",
                           "job_states",
                           "depend", ctx->jsctx->depend_count,
                           "sched", ctx->jsctx->sched_count,
                           "run", ctx->jsctx->run_count,
                           "cleanup", ctx->jsctx->cleanup_count,
                           "inactive", ctx->jsctx->inactive_count,
                           "total", total) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.lookup",
      .cb           = lookup_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.eventlog-watch",
      .cb           = watch_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.eventlog-watch-cancel",
      .cb           = watch_cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.guest-eventlog-watch",
      .cb           = guest_watch_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.guest-eventlog-watch-cancel",
      .cb           = guest_watch_cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.list",
      .cb           = list_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.list-id",
      .cb           = list_id_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.list-attrs",
      .cb           = list_attrs_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.job-state-pause",
      .cb           = job_state_pause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.job-state-unpause",
      .cb           = job_state_unpause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.job-stats",
      .cb           = job_stats_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "job-state",
      .cb           = job_state_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void info_ctx_destroy (struct info_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        /* freefn set on lookup entries will destroy list entries */
        if (ctx->lookups)
            zlist_destroy (&ctx->lookups);
        if (ctx->watchers) {
            watch_cleanup (ctx);
            zlist_destroy (&ctx->watchers);
        }
        if (ctx->guest_watchers) {
            guest_watch_cleanup (ctx);
            zlist_destroy (&ctx->guest_watchers);
        }
        if (ctx->jsctx)
            job_state_destroy (ctx->jsctx);
        if (ctx->idsync_lookups)
            idsync_cleanup (ctx);
        free (ctx);
        errno = saved_errno;
    }
}

static struct info_ctx *info_ctx_create (flux_t *h)
{
    struct info_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->lookups = zlist_new ()))
        goto error;
    if (!(ctx->watchers = zlist_new ()))
        goto error;
    if (!(ctx->guest_watchers = zlist_new ()))
        goto error;
    if (!(ctx->jsctx = job_state_create (h)))
        goto error;
    if (idsync_setup (ctx) < 0)
        goto error;
    return ctx;
error:
    info_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct info_ctx *ctx;
    int rc = -1;

    if (!(ctx = info_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    if (job_state_init_from_kvs (ctx) < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    info_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("job-info");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
