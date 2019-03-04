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
#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "util.h"
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

/* Enqueue jobs from 'jobs' array in queue.
 * On success, return a list of struct job's.
 * On failure, return NULL with errno set (no jobs enqueued).
 */
zlist_t *enqueue_jobs (struct queue *queue, json_t *jobs)
{
    size_t index;
    json_t *el;
    zlist_t *newjobs;
    struct job *job;
    int saved_errno;

    if (!(newjobs = zlist_new ()))
        goto error;
    json_array_foreach (jobs, index, el) {
        flux_jobid_t id;
        uint32_t userid;
        int priority;
        double t_submit;
        int flags;

        if (json_unpack (el, "{s:I s:i s:i s:f s:i}", "id", &id,
                                                      "priority", &priority,
                                                      "userid", &userid,
                                                      "t_submit", &t_submit,
                                                      "flags", &flags) < 0) {
            goto error;
        }
        if (!(job = job_create (id, priority, userid, t_submit, flags)))
            goto error;
        if (queue_insert (queue, job, &job->queue_handle) < 0) {
            job_decref (job);
            if (errno == EEXIST)
                continue; // don't report error - might be a race with restart
            goto error;
        }
        if (zlist_push (newjobs, job) < 0) {
            queue_delete (queue, job, job->queue_handle);
            job_decref (job);
            errno = ENOMEM;
            goto error;
        }
    }
    return newjobs;
error:
    saved_errno = errno;
    while ((job = zlist_pop (newjobs))) {
        queue_delete (queue, job, job->queue_handle);
        job_decref (job);
    }
    zlist_destroy (&newjobs);
    errno = saved_errno;
    return NULL;
}

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    json_t *jobs;
    zlist_t *newjobs;
    struct job *job;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (!(newjobs = enqueue_jobs (ctx->queue, jobs))) {
        flux_log_error (h, "%s: error enqueuing batch", __FUNCTION__);
        goto error;
    }
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__,
                            (int)zlist_size (newjobs));
    /* Submitting user is being responded to with jobid's.
     * Now walk the list of new jobs and advance their state.
     */
    while ((job = zlist_pop (newjobs))) {
        job->state = FLUX_JOB_SCHED;
        if (alloc_do_request (ctx->alloc_ctx, job) < 0)
            flux_log_error (h, "%s: error notifying scheduler of new job %llu",
                            __FUNCTION__, (unsigned long long)job->id);
        job_decref (job);
    }
    zlist_destroy (&newjobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* list request handled in list.c
 */
static void list_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;

    list_handle_request (h, ctx->queue, msg);
}

/* exception request handled in raise.c
 */
static void raise_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    raise_handle_request (h, ctx->queue, ctx->alloc_ctx, msg);
}

/* priority request handled in priority.c
 */
static void priority_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    struct job_manager_ctx *ctx = arg;
    priority_handle_request (h, ctx->queue, msg);
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
