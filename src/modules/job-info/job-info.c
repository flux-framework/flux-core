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
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job-info.h"
#include "allow.h"
#include "lookup.h"
#include "watch.h"
#include "guest_watch.h"
#include "update.h"

static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct info_ctx *ctx = arg;
    watchers_cancel (ctx, msg, false);
    guest_watchers_cancel (ctx, msg, false);
    update_watchers_cancel (ctx, msg, false);
}

static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct info_ctx *ctx = arg;
    int lookups = zlist_size (ctx->lookups);
    int watchers = zlist_size (ctx->watchers);
    int guest_watchers = zlist_size (ctx->guest_watchers);
    int update_lookups = 0;     /* no longer supported */
    int update_watchers = update_watch_count (ctx);
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i s:i s:i}",
                           "lookups", lookups,
                           "watchers", watchers,
                           "guest_watchers", guest_watchers,
                           "update_lookups", update_lookups,
                           "update_watchers", update_watchers) < 0) {
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
      .topic_glob   = "job-info.update-lookup",
      .cb           = update_lookup_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.update-watch",
      .cb           = update_watch_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.update-watch-cancel",
      .cb           = update_watch_cancel_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-info.stats-get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void info_ctx_destroy (struct info_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        lru_cache_destroy (ctx->owner_lru);
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
        if (ctx->update_watchers) {
            update_watch_cleanup (ctx);
            zlist_destroy (&ctx->update_watchers);
        }
        if (ctx->index_uw)
            zhashx_destroy (&ctx->index_uw);
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
    if (!(ctx->owner_lru = lru_cache_create (OWNER_LRU_MAXSIZE)))
        goto error;
    lru_cache_set_free_f (ctx->owner_lru, (lru_cache_free_f)free);
    if (!(ctx->lookups = zlist_new ()))
        goto error;
    if (!(ctx->watchers = zlist_new ()))
        goto error;
    if (!(ctx->guest_watchers = zlist_new ()))
        goto error;
    if (!(ctx->update_watchers = zlist_new ()))
        goto error;
    /* no destructor for index_uw, destruction handled on
     * update_watchers list */
    if (!(ctx->index_uw = zhashx_new ()))
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
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    info_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
