/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* submit.c - handle job-manager.submit request from job-ingest */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "alloc.h"
#include "event.h"

#include "submit.h"

#include "src/common/libeventlog/eventlog.h"

struct submit_ctx {
    flux_t *h;
    bool submit_disable;
    flux_msg_handler_t **handlers;
    struct queue *queue;
    struct event_ctx *event_ctx;
};

/* Decode 'o' into a struct job, then add it to the queue.
 * Also record the job in 'newjobs'.
 */
int submit_enqueue_one_job (struct queue *queue, zlist_t *newjobs, json_t *o)
{
    struct job *job;

    if (!(job = job_create ()))
        return -1;
    if (json_unpack (o,
                     "{s:I s:i s:i s:f s:i}",
                     "id",
                     &job->id,
                     "priority",
                     &job->priority,
                     "userid",
                     &job->userid,
                     "t_submit",
                     &job->t_submit,
                     "flags",
                     &job->flags)
        < 0) {
        errno = EPROTO;
        job_decref (job);
        return -1;
    }
    if (queue_insert (queue, job, &job->queue_handle) < 0) {
        job_decref (job);
        // EEXIST is not an error - there is a window for restart_from_kvs()
        // to pick up a job that also has a submit request in flight.
        return (errno == EEXIST ? 0 : -1);
    }
    if (zlist_push (newjobs, job) < 0) {
        queue_delete (queue, job, job->queue_handle);
        job_decref (job);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* The submit request has failed.  Dequeue jobs recorded in 'newjobs',
 * then destroy the newjobs list.
 */
void submit_enqueue_jobs_cleanup (struct queue *queue, zlist_t *newjobs)
{
    if (newjobs) {
        int saved_errno = errno;
        struct job *job;
        while ((job = zlist_pop (newjobs))) {
            queue_delete (queue, job, job->queue_handle);
            job_decref (job);
        }
        zlist_destroy (&newjobs);
        errno = saved_errno;
    }
}

/* Enqueue jobs from 'jobs' array in queue.
 * On success, return a list of struct job's.
 * On failure, return NULL with errno set (no jobs enqueued).
 */
zlist_t *submit_enqueue_jobs (struct queue *queue, json_t *jobs)
{
    size_t index;
    json_t *el;
    zlist_t *newjobs;

    if (!(newjobs = zlist_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    json_array_foreach (jobs, index, el)
    {
        if (submit_enqueue_one_job (queue, newjobs, el) < 0)
            goto error;
    }
    return newjobs;
error:
    submit_enqueue_jobs_cleanup (queue, newjobs);
    return NULL;
}

/* Submit event requires special handling.  It cannot go through
 * event_job_post_pack() because job-ingest already logged it.
 * However, we want to let the state machine choose the next state and action,
 * We instead re-create the event and run it directly through
 * event_job_update() and event_job_action().
 */
int submit_post_event (struct event_ctx *event_ctx, struct job *job)
{
    json_t *entry = NULL;
    int rv = -1;

    entry = eventlog_entry_pack (job->t_submit,
                                 "submit",
                                 "{ s:i s:i s:i }",
                                 "userid",
                                 job->userid,
                                 "priority",
                                 job->priority,
                                 "flags",
                                 job->flags);
    if (!entry)
        goto error;
    if (event_job_update (job, entry) < 0) /* NEW -> DEPEND */
        goto error;
    if (event_batch_pub_state (event_ctx, job) < 0)
        goto error;
    if (event_job_action (event_ctx, job) < 0)
        goto error;
    rv = 0;
error:
    json_decref (entry);
    return rv;
}

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h,
                       flux_msg_handler_t *mh,
                       const flux_msg_t *msg,
                       void *arg)
{
    struct submit_ctx *ctx = arg;
    json_t *jobs;
    zlist_t *newjobs;
    struct job *job;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (ctx->submit_disable) {
        errno = EINVAL;
        errmsg = "job submission is disabled";
        goto error;
    }
    if (!(newjobs = submit_enqueue_jobs (ctx->queue, jobs))) {
        flux_log_error (h, "%s: error enqueuing batch", __FUNCTION__);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_log (h,
              LOG_DEBUG,
              "%s: added %d jobs",
              __FUNCTION__,
              (int)zlist_size (newjobs));

    /* Submitting user is being responded to with jobid's.
     * Now walk the list of new jobs and advance their state.
     */
    while ((job = zlist_pop (newjobs))) {
        if (submit_post_event (ctx->event_ctx, job) < 0)
            flux_log_error (h,
                            "%s: submit_post_event id=%llu",
                            __FUNCTION__,
                            (unsigned long long)job->id);
        job_decref (job);
    }
    zlist_destroy (&newjobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void submit_enable (struct submit_ctx *ctx)
{
    ctx->submit_disable = false;
}

void submit_disable (struct submit_ctx *ctx)
{
    ctx->submit_disable = true;
}

void submit_ctx_destroy (struct submit_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {FLUX_MSGTYPE_REQUEST, "job-manager.submit", submit_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct submit_ctx *submit_ctx_create (flux_t *h,
                                      struct queue *queue,
                                      struct event_ctx *event_ctx)
{
    struct submit_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    ctx->queue = queue;
    ctx->event_ctx = event_ctx;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    return ctx;
error:
    submit_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
