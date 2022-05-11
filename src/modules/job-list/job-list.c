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

#include "job-list.h"
#include "job_state.h"
#include "list.h"
#include "idsync.h"

static const char *attrs[] = {
    "userid", "urgency", "priority", "t_submit",
    "t_depend", "t_run", "t_cleanup", "t_inactive",
    "state", "states_mask", "name", "ntasks", "nnodes",
    "ranks", "nodelist", "success", "exception_occurred",
    "exception_type", "exception_severity",
    "exception_note", "result", "expiration",
    "annotations", "waitstatus", "dependencies",
    NULL
};

const char **job_attrs (void)
{
    return attrs;
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    int pending = zlistx_size (ctx->jsctx->pending);
    int running = zlistx_size (ctx->jsctx->running);
    int inactive = zlistx_size (ctx->jsctx->inactive);
    int idsync_lookups = zlistx_size (ctx->idsync_lookups);
    int idsync_waits = zhashx_size (ctx->idsync_waits);
    if (flux_respond_pack (h, msg, "{s:{s:i s:i s:i} s:{s:i s:i}}",
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
    struct list_ctx *ctx = arg;
    json_t *o = job_stats_encode (&ctx->jsctx->stats);
    if (o == NULL)
        goto error;
    if (flux_respond_pack (h, msg, "o", o) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void purge_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct list_ctx *ctx = arg;
    json_t *jobs;
    size_t index;
    json_t *entry;
    int count = 0;

    if (flux_event_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0)
        flux_log_error (h, "job-purge-inactive message");
    json_array_foreach (jobs, index, entry) {
        flux_jobid_t id = json_integer_value (entry);
        struct job *job;

        if ((job = zhashx_lookup (ctx->jsctx->index, &id))) {
            job_stats_purge (&ctx->jsctx->stats, job);
            if (job->list_handle)
                zlistx_delete (ctx->jsctx->inactive, job->list_handle);
            zhashx_delete (ctx->jsctx->index, &id);
            count++;
        }
    }
    flux_log (h, LOG_DEBUG, "purged %d inactive jobs", count);
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list",
      .cb           = list_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-inactive",
      .cb           = list_inactive_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-id",
      .cb           = list_id_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list-attrs",
      .cb           = list_attrs_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-state-pause",
      .cb           = job_state_pause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-state-unpause",
      .cb           = job_state_unpause_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.job-stats",
      .cb           = job_stats_cb,
      .rolemask     = FLUX_ROLE_USER
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.stats.get",
      .cb           = stats_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "job-purge-inactive",
      .cb           = purge_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void list_ctx_destroy (struct list_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->jsctx)
            job_state_destroy (ctx->jsctx);
        if (ctx->idsync_lookups)
            idsync_cleanup (ctx);
        free (ctx);
        errno = saved_errno;
    }
}

static struct list_ctx *list_ctx_create (flux_t *h)
{
    struct list_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;
    if (flux_event_subscribe (h, "job-purge-inactive") < 0)
        goto error;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (!(ctx->jsctx = job_state_create (ctx)))
        goto error;
    if (idsync_setup (ctx) < 0)
        goto error;
    return ctx;
error:
    list_ctx_destroy (ctx);
    return NULL;
}

int mod_main (flux_t *h, int argc, char **argv)
{
    struct list_ctx *ctx;
    int rc = -1;

    if (!(ctx = list_ctx_create (h))) {
        flux_log_error (h, "initialization error");
        goto done;
    }
    if (job_state_init_from_kvs (ctx) < 0)
        goto done;
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    list_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("job-list");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
