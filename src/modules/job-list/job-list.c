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
#include "job_data.h"
#include "list.h"
#include "idsync.h"
#include "stats.h"

static const char *attrs[] = {
    "userid", "urgency", "priority", "t_submit",
    "t_depend", "t_run", "t_cleanup", "t_inactive",
    "state", "name", "cwd", "queue", "project", "bank",
    "ntasks", "ncores", "duration", "nnodes",
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

static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct list_ctx *ctx = arg;

    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }

    int pending = zlistx_size (ctx->jsctx->pending);
    int running = zlistx_size (ctx->jsctx->running);
    int inactive = zlistx_size (ctx->jsctx->inactive);
    int idsync_lookups = zlistx_size (ctx->isctx->lookups);
    int idsync_waits = zhashx_size (ctx->isctx->waits);
    int stats_watchers = job_stats_watchers (ctx->jsctx->statsctx);
    if (flux_respond_pack (h, msg, "{s:{s:i s:i s:i} s:{s:i s:i} s:i}",
                           "jobs",
                           "pending", pending,
                           "running", running,
                           "inactive", inactive,
                           "idsync",
                           "lookups", idsync_lookups,
                           "waits", idsync_waits,
                           "stats_watchers", stats_watchers) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
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
            if (job->state != FLUX_JOB_STATE_INACTIVE)
                continue;
            job_stats_purge (ctx->jsctx->statsctx, job);
            if (job->list_handle)
                zlistx_delete (ctx->jsctx->inactive, job->list_handle);
            zhashx_delete (ctx->jsctx->index, &id);
            count++;
        }
    }
    flux_log (h, LOG_DEBUG, "purged %d inactive jobs", count);
}

void requeue_deferred_requests (struct list_ctx *ctx)
{
    const flux_msg_t *msg;

    while ((msg = flux_msglist_pop (ctx->deferred_requests))) {
        if (flux_requeue (ctx->h, msg, FLUX_RQ_TAIL) < 0)
            flux_log_error (ctx->h, "error requeuing deferred request");
        flux_msg_decref (msg);
    }
}

static void disconnect_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct list_ctx *ctx = arg;
    job_stats_disconnect (ctx->jsctx->statsctx, msg);
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct list_ctx *ctx = arg;
    flux_conf_t *conf;
    flux_error_t error;
    const char *errstr = NULL;

    if (flux_module_config_request_decode (msg, &conf) < 0)
        goto error;
    if (job_state_config_reload (ctx->jsctx, conf, &error) < 0) {
        errstr = error.text;
        goto error_decref;
    }
    if (job_match_config_reload (ctx->mctx, conf, &error) < 0) {
        errstr = error.text;
        goto error_decref;
    }
    if (flux_set_conf_new (h, conf) < 0) {
        errstr = "error updating config";
        goto error_decref;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error_decref:
    flux_conf_decref (conf);
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[] = {
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.list",
      .cb           = list_cb,
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
      .topic_glob   = "job-list.stats-get",
      .cb           = stats_cb,
      .rolemask     = FLUX_ROLE_USER,
    },
    { .typemask     = FLUX_MSGTYPE_EVENT,
      .topic_glob   = "job-purge-inactive",
      .cb           = purge_cb,
      .rolemask     = 0
    },
    { .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.disconnect",
      .cb           = disconnect_cb,
      .rolemask     = FLUX_ROLE_USER,
    },
    {
      .typemask     = FLUX_MSGTYPE_REQUEST,
      .topic_glob   = "job-list.config-reload",
      .cb           = config_reload_cb,
      .rolemask     = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

static void list_ctx_destroy (struct list_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        flux_msglist_destroy (ctx->deferred_requests);
        if (ctx->jsctx)
            job_state_destroy (ctx->jsctx);
        if (ctx->isctx)
            idsync_ctx_destroy (ctx->isctx);
        if (ctx->mctx)
            match_ctx_destroy (ctx->mctx);
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
    if (!(ctx->isctx = idsync_ctx_create (ctx->h)))
        goto error;
    if (!(ctx->jsctx = job_state_create (ctx)))
        goto error;
    if (!(ctx->deferred_requests = flux_msglist_create ()))
        goto error;
    if (!(ctx->mctx = match_ctx_create (ctx->h)))
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
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0)
        goto done;
    rc = 0;
done:
    list_ctx_destroy (ctx);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
