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

#include "job.h"
#include "queue.h"
#include "submit.h"
#include "restart.h"
#include "raise.h"
#include "list.h"
#include "priority.h"
#include "alloc.h"
#include "event.h"


struct job_manager_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct queue *queue;
    struct alloc_ctx *alloc_ctx;
    struct event_ctx *event_ctx;
};

static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    submit_handle_request (h, ctx->queue, ctx->alloc_ctx, msg);
}

static void list_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    list_handle_request (h, ctx->queue, msg);
}

static void raise_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    raise_handle_request (h, ctx->queue, ctx->event_ctx, ctx->alloc_ctx, msg);
}

static void priority_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    priority_handle_request (h, ctx->queue, ctx->event_ctx, msg);
}

/* reload_map_f callback
 * This is called for each job encountered in the KVS during startup.
 * The 'job' struct is valid only during the callback.
 * queue_insert() increments the 'job' usecount upon successful insert.
 */
static int restart_map_cb (struct job *job, void *arg)
{
    struct job_manager_ctx *ctx = arg;

    if (queue_insert (ctx->queue, job, &job->queue_handle) < 0)
        return -1;
    if (job->state == FLUX_JOB_NEW)
        job->state = FLUX_JOB_SCHED;
    if (job->state == FLUX_JOB_SCHED) {
        if (alloc_do_request (ctx->alloc_ctx, job) < 0) {
            flux_log_error (ctx->h, "%s: error notifying scheduler of job %llu",
                __FUNCTION__, (unsigned long long)job->id);
        }
    }
    return 0;
}

/* Load any active jobs present in the KVS at startup.
 */
static int restart_from_kvs (flux_t *h, struct job_manager_ctx *ctx)
{
    int count;

    if ((count = restart_map (h, restart_map_cb, ctx)) < 0)
        return -1;
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__, count);
    return 0;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.submit", submit_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.list", list_cb, FLUX_ROLE_USER},
    { FLUX_MSGTYPE_REQUEST, "job-manager.raise", raise_cb, FLUX_ROLE_USER},
    { FLUX_MSGTYPE_REQUEST, "job-manager.priority", priority_cb, FLUX_ROLE_USER},
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_manager_ctx ctx;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;

    if (!(ctx.queue = queue_create (true))) {
        flux_log_error (h, "error creating queue");
        goto done;
    }
    if (!(ctx.event_ctx = event_ctx_create (h))) {
        flux_log_error (h, "error creating event batcher");
        goto done;
    }
    if (!(ctx.alloc_ctx = alloc_ctx_create (h, ctx.queue, ctx.event_ctx))) {
        flux_log_error (h, "error creating scheduler interface");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (restart_from_kvs (h, &ctx) < 0) {
        flux_log_error (h, "restart_from_kvs");
        goto done;
    }
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    alloc_ctx_destroy (ctx.alloc_ctx);
    event_ctx_destroy (ctx.event_ctx);
    queue_destroy (ctx.queue);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
