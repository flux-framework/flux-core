/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* event.c - job state machine and eventlog commit batching
 *
 * event_job_update() implements the job state machine described
 * in RFC 21.  This function is called when an event occurs for a job,
 * to drive changes to job state and flags.  For example, an "alloc"
 * event transitions a job from SCHED to RUN state.
 *
 * event_job_action() is called after event_job_update().  It takes actions
 * appropriate for job state and flags.  For example, in RUN state,
 * job shells are started.
 *
 * Events are logged in the job eventlog in the KVS.  For performance,
 * multiple updates may be combined into one commit.  The location of
 * the job eventlog and its contents are described in RFC 16 and RFC 18.
 *
 * The function event_job_post_pack() posts an event to a job, running
 * event_job_update(), event_job_action(), and committing the event to
 * the job eventlog, in a delayed batch.
 *
 * Notes:
 * - A KVS commit failure is handled as fatal to the job-manager
 * - event_job_action() is idempotent
 * - event_ctx_destroy() flushes batched eventlog updates before returning
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "alloc.h"
#include "start.h"

#include "event.h"

#include "src/common/libeventlog/eventlog.h"

const double batch_timeout = 0.01;

struct event_ctx {
    flux_t *h;
    struct queue *queue;
    struct alloc_ctx *alloc_ctx;
    struct start_ctx *start_ctx;
    struct event_batch *batch;
    flux_watcher_t *timer;
    zlist_t *pending;
    zlist_t *pub_futures;
};

struct event_batch {
    struct event_ctx *ctx;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    json_t *state_trans;
};

struct event_batch *event_batch_create (struct event_ctx *ctx);
void event_batch_destroy (struct event_batch *batch);

/* Batch commit has completed.
 * If there was a commit error, log it and stop the reactor.
 * Destroy 'batch'.
 */
void commit_continuation (flux_future_t *f, void *arg)
{
    struct event_batch *batch = arg;
    struct event_ctx *ctx = batch->ctx;

    if (flux_future_get (batch->f, NULL) < 0) {
        flux_log_error (ctx->h, "%s: eventlog update failed", __FUNCTION__);
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
    }
    zlist_remove (ctx->pending, batch);
    event_batch_destroy (batch);
}

/* job-state event publish has completed.
 * If there was a publish error, log it and stop the reactor.
 * Destroy 'f'.
 */
void publish_continuation (flux_future_t *f, void *arg)
{
    struct event_ctx *ctx = arg;

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (ctx->h, "%s: event publish failed", __FUNCTION__);
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
    }
    zlist_remove (ctx->pub_futures, f);
    flux_future_destroy (f);
}

/* Close the current batch, if any, and commit it.
 */
void event_batch_commit (struct event_ctx *ctx)
{
    struct event_batch *batch = ctx->batch;
    if (batch) {
        ctx->batch = NULL;
        if (batch->txn) {
            if (!(batch->f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn)))
                goto error;
            if (flux_future_then (batch->f, -1., commit_continuation, batch) < 0)
                goto error;
            if (zlist_append (ctx->pending, batch) < 0)
                goto nomem;
        }
        else { // just publish events and be done
            event_batch_destroy (batch);
        }
    }
    return;
nomem:
    errno = ENOMEM;
error: // unlikely (e.g. ENOMEM)
    flux_log_error (ctx->h, "%s: aborting reactor", __FUNCTION__);
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
    event_batch_destroy (batch);
}

void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct event_ctx *ctx = arg;
    event_batch_commit (ctx);
}

void event_publish_state (struct event_ctx *ctx, json_t *state_trans)
{
    flux_future_t *f;

    if (!(f = flux_event_publish_pack (ctx->h, "job-state", 0, "{s:O}",
                                               "transitions", state_trans))) {
        flux_log_error (ctx->h, "%s: flux_event_publish_pack", __FUNCTION__);
        goto error;
    }
    if (flux_future_then (f, -1., publish_continuation, ctx) < 0) {
        flux_future_destroy (f);
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }
    if (zlist_append (ctx->pub_futures, f) < 0) {
        flux_future_destroy (f);
        flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
}

void event_batch_destroy (struct event_batch *batch)
{
    if (batch) {
        int saved_errno = errno;

        flux_kvs_txn_destroy (batch->txn);
        if (batch->f)
            (void)flux_future_wait_for (batch->f, -1);
        if (batch->state_trans) {
            event_publish_state (batch->ctx, batch->state_trans);
            json_decref (batch->state_trans);
        }
        flux_future_destroy (batch->f);
        free (batch);
        errno = saved_errno;
    }
}

struct event_batch *event_batch_create (struct event_ctx *ctx)
{
    struct event_batch *batch;

    if (!(batch = calloc (1, sizeof (*batch))))
        return NULL;
    if (!(batch->state_trans = json_array ()))
        goto nomem;
    batch->ctx = ctx;
    return batch;
nomem:
    errno = ENOMEM;
    event_batch_destroy (batch);
    return NULL;
}

/* Create a new "batch" if there is none.
 * No-op if batch already started.
 */
int event_batch_start (struct event_ctx *ctx)
{
    if (!ctx->batch) {
        if (!(ctx->batch = event_batch_create (ctx)))
            return -1;
        flux_timer_watcher_reset (ctx->timer, batch_timeout, 0.);
        flux_watcher_start (ctx->timer);
    }
    return 0;
}

static int event_batch_commit_event (struct event_ctx *ctx, struct job *job,
                                     json_t *entry)
{
    char key[64];
    char *entrystr = NULL;

    if (event_batch_start (ctx) < 0)
        return -1;
    if (flux_job_kvs_key (key, sizeof (key), true, job->id, "eventlog") < 0)
        return -1;
    if (!ctx->batch->txn && !(ctx->batch->txn = flux_kvs_txn_create ()))
        return -1;
    if (!(entrystr = eventlog_entry_encode (entry)))
        return -1;
    if (flux_kvs_txn_put (ctx->batch->txn, FLUX_KVS_APPEND, key, entrystr) < 0) {
        free (entrystr);
        return -1;
    }
    free (entrystr);
    return 0;
}

int event_batch_pub_state (struct event_ctx *ctx, struct job *job)
{
    json_t *o;

    if (event_batch_start (ctx) < 0)
        goto error;
    if (!(o = json_pack ("[I,s]", job->id,
                         flux_job_statetostr (job->state, false))))
        goto nomem;
    if (json_array_append_new (ctx->batch->state_trans, o)) {
        json_decref (o);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
error:
    return -1;
}

int event_job_action (struct event_ctx *ctx, struct job *job)
{
    switch (job->state) {
        case FLUX_JOB_NEW:
            break;
        case FLUX_JOB_DEPEND:
            if (event_job_post_pack (ctx, job, "depend", NULL) < 0)
                return -1;
            break;
        case FLUX_JOB_SCHED:
            if (alloc_enqueue_alloc_request (ctx->alloc_ctx, job) < 0)
                return -1;
            break;
        case FLUX_JOB_RUN:
            if (start_send_request (ctx->start_ctx, job) < 0)
                return -1;
            break;
        case FLUX_JOB_CLEANUP:
            if (job->alloc_queued)
                alloc_dequeue_alloc_request (ctx->alloc_ctx, job);

            /* N.B. start_pending indicates that the start request is still
             * expecting responses.  The final response is the 'release'
             * response with final=true.  Thus once the flag is clear,
             * it is safe to release all resources to the scheduler.
             */
            if (job->has_resources && !job->start_pending) {
                if (alloc_send_free_request (ctx->alloc_ctx, job) < 0)
                    return -1;
            }
            /* Post cleanup event when cleanup is complete.
             */
            if (!job->alloc_queued && !job->alloc_pending
                                   && !job->free_pending
                                   && !job->start_pending
                                   && !job->has_resources) {

                if (event_job_post_pack (ctx, job, "clean", NULL) < 0)
                    return -1;
            }
            break;
        case FLUX_JOB_INACTIVE:
            queue_delete (ctx->queue, job, job->queue_handle);
            break;
    }
    return 0;
}

int event_submit_context_decode (json_t *context,
                                 int *priority,
                                 uint32_t *userid,
                                 int *flags)
{
    if (json_unpack (context, "{ s:i s:i s:i }",
                     "priority", priority,
                     "userid", userid,
                     "flags", flags) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int event_priority_context_decode (json_t *context,
                                   int *priority)
{
    if (json_unpack (context, "{ s:i }", "priority", priority) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int event_exception_context_decode (json_t *context,
                                    int *severity)
{
    if (json_unpack (context, "{ s:i }", "severity", severity) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int event_release_context_decode (json_t *context,
                                  int *final)
{
    *final = 0;

    if (json_unpack (context, "{ s:b }", "final", &final) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

int event_job_update (struct job *job, json_t *event)
{
    double timestamp;
    const char *name;
    json_t *context;

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        goto error;

    if (!strcmp (name, "submit")) {
        if (job->state != FLUX_JOB_NEW)
            goto inval;
        job->t_submit = timestamp;
        if (event_submit_context_decode (context,
                                         &job->priority,
                                         &job->userid,
                                         &job->flags) < 0)
            goto error;
        job->state = FLUX_JOB_DEPEND;
    }
    if (!strcmp (name, "depend")) {
        if (job->state != FLUX_JOB_DEPEND)
            goto inval;
        job->state = FLUX_JOB_SCHED;
    }
    else if (!strcmp (name, "priority")) {
        if (event_priority_context_decode (context, &job->priority) < 0)
            goto error;
    }
    else if (!strcmp (name, "exception")) {
        int severity;
        if (job->state == FLUX_JOB_NEW || job->state == FLUX_JOB_INACTIVE)
            goto inval;
        if (event_exception_context_decode (context, &severity) < 0)
            goto error;
        if (severity == 0)
            job->state = FLUX_JOB_CLEANUP;
    }
    else if (!strcmp (name, "alloc")) {
        if (job->state != FLUX_JOB_SCHED && job->state != FLUX_JOB_CLEANUP)
            goto inval;
        job->has_resources = 1;
        if (job->state == FLUX_JOB_SCHED)
            job->state = FLUX_JOB_RUN;
    }
    else if (!strcmp (name, "free")) {
        if (job->state != FLUX_JOB_CLEANUP)
            goto inval;
        job->has_resources = 0;
    }
    else if (!strcmp (name, "finish")) {
        if (job->state != FLUX_JOB_RUN && job->state != FLUX_JOB_CLEANUP)
            goto inval;
        if (job->state == FLUX_JOB_RUN)
            job->state = FLUX_JOB_CLEANUP;
    }
    else if (!strcmp (name, "release")) {
        int final;
        if (job->state != FLUX_JOB_RUN && job->state != FLUX_JOB_CLEANUP)
            goto inval;
        if (event_release_context_decode (context, &final) < 0)
            goto error;
        if (final && job->state == FLUX_JOB_RUN)
            goto inval;
    }
    else if (!strcmp (name, "clean")) {
        if (job->state != FLUX_JOB_CLEANUP)
            goto inval;
        job->state = FLUX_JOB_INACTIVE;
    }
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

int event_job_post_pack (struct event_ctx *ctx, struct job *job,
                         const char *name, const char *context_fmt, ...)
{
    va_list ap;
    json_t *entry = NULL;
    int saved_errno;
    flux_job_state_t old_state = job->state;

    va_start (ap, context_fmt);
    if (!(entry = eventlog_entry_vpack (0., name, context_fmt, ap)))
        return -1;
    if (event_job_update (job, entry) < 0) // modifies job->state
        goto error;
    if (event_batch_commit_event (ctx, job, entry) < 0)
        goto error;
    if (job->state != old_state) {
        if (event_batch_pub_state (ctx, job) < 0)
            goto error;
    }
    if (event_job_action (ctx, job) < 0)
        goto error;
    json_decref (entry);
    va_end (ap);
    return 0;
error:
    saved_errno = errno;
    json_decref (entry);
    va_end (ap);
    errno = saved_errno;
    return -1;
}

void event_ctx_set_alloc_ctx (struct event_ctx *ctx,
                              struct alloc_ctx *alloc_ctx)
{
    ctx->alloc_ctx = alloc_ctx;
}

void event_ctx_set_start_ctx (struct event_ctx *ctx,
                              struct start_ctx *start_ctx)
{
    ctx->start_ctx = start_ctx;
}

/* Finalizes in-flight batch KVS commits and event pubs (synchronously).
 */
void event_ctx_destroy (struct event_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_watcher_destroy (ctx->timer);
        event_batch_commit (ctx);
        if (ctx->pending) {
            struct event_batch *batch;
            while ((batch = zlist_pop (ctx->pending)))
                event_batch_destroy (batch); // N.B. can append to pub_futures
        }
        zlist_destroy (&ctx->pending);
        if (ctx->pub_futures) {
            flux_future_t *f;
            while ((f = zlist_pop (ctx->pub_futures))) {
                if (flux_future_get (f, NULL) < 0)
                    flux_log_error (ctx->h, "error publishing job-state event");
                flux_future_destroy (f);
            }
        }
        zlist_destroy (&ctx->pub_futures);
        free (ctx);
        errno = saved_errno;
    }
}

struct event_ctx *event_ctx_create (flux_t *h, struct queue *queue)
{
    struct event_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    ctx->queue = queue;
    if (!(ctx->timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                  0., 0., timer_cb, ctx)))
        goto error;
    if (!(ctx->pending = zlist_new ()))
        goto nomem;
    if (!(ctx->pub_futures = zlist_new ()))
        goto nomem;
    return ctx;
nomem:
    errno = ENOMEM;
error:
    event_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

