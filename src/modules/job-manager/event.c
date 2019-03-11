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
 * appropriate for job state and flags.  flags.  For example, in RUN state,
 * job shells are started.
 *
 * Events are logged in the job eventlog in the KVS.  For performance,
 * multiple updates may be combined into one commit.  The location of
 * the job eventlog and its contents are described in RFC 16 and RFC 18.
 *
 * The functions event_job_post() and event_job_post_pack() post an event to a
 * job, running event_job_update(), event_job_action(), and committing the
 * event to the job eventlog, in a delayed batch.
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

#include "event.h"

const double batch_timeout = 0.01;

struct event_ctx {
    flux_t *h;
    struct alloc_ctx *alloc_ctx;
    struct event_batch *batch;
    flux_watcher_t *timer;
    zlist_t *pending;
};

struct event_batch {
    struct event_ctx *ctx;
    flux_kvs_txn_t *txn;
    zlist_t *callbacks;
    flux_future_t *f;
};

struct event_callback {
    flux_continuation_f cb;
    void *arg;
};

struct event_batch *event_batch_create (struct event_ctx *ctx);
void event_batch_destroy (struct event_batch *batch);

/* Batch commit has completed.
 * If there was a commit error, log it and stop the reactor.
 * Destroy 'batch', which notifies any registered callbacks.
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

/* Close the current batch, if any, and commit it.
 */
void event_batch_commit (struct event_ctx *ctx)
{
    struct event_batch *batch = ctx->batch;
    if (batch) {
        ctx->batch = NULL;
        if (!(batch->f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn)))
            goto error;
        if (flux_future_then (batch->f, -1., commit_continuation, batch) < 0)
            goto error;
        if (zlist_append (ctx->pending, batch) < 0)
            goto nomem;
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

void event_batch_destroy (struct event_batch *batch)
{
    if (batch) {
        int saved_errno = errno;
        struct event_callback *ec;

        flux_kvs_txn_destroy (batch->txn);
        if (batch->f)
            (void)flux_future_wait_for (batch->f, -1);
        if (batch->callbacks) {
            while ((ec = zlist_pop (batch->callbacks))) {
                ec->cb (batch->f, ec->arg);
                free (ec);
            }
            zlist_destroy (&batch->callbacks);
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
    if (!(batch->txn = flux_kvs_txn_create ()))
        goto error;
    if (!(batch->callbacks = zlist_new ()))
        goto nomem;
    batch->ctx = ctx;
    return batch;
nomem:
    errno = ENOMEM;
error:
    event_batch_destroy (batch);
    return NULL;
}

/* Append event to batch, registering callback 'cb' if non-NULL.
 */
int event_batch_append (struct event_batch *batch,
                        const char *key, const char *event,
                        flux_continuation_f cb, void *arg)
{
    if (cb) {
        struct event_callback *ec;
        if (!(ec = calloc (1, sizeof (*ec))))
            return -1;
        ec->cb = cb;
        ec->arg = arg;
        if (zlist_push (batch->callbacks, ec) < 0) {
            free (ec);
            errno = ENOMEM;
            return -1;
        }
    }
    if (flux_kvs_txn_put (batch->txn, FLUX_KVS_APPEND, key, event) < 0) {
        if (cb) {
            struct event_callback *ec;
            int saved_errno = errno;
            ec = zlist_pop (batch->callbacks);
            free (ec);
            errno = saved_errno;
        }
        return -1;
    }
    return 0;
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

int event_job_action (struct event_ctx *ctx, struct job *job)
{
    switch (job->state) {
        case FLUX_JOB_NEW:
        case FLUX_JOB_DEPEND:
            break;
        case FLUX_JOB_SCHED:
            if (alloc_enqueue_alloc_request (ctx->alloc_ctx, job) < 0)
                return -1;
            break;
        case FLUX_JOB_RUN:
            break;
        case FLUX_JOB_CLEANUP:
            if (job->has_resources) {
                if (alloc_send_free_request (ctx->alloc_ctx, job) < 0)
                    return -1;
            }
            break;
        case FLUX_JOB_INACTIVE:
            break;
    }
    return 0;
}

int event_submit_context_decode (const char *context,
                                 int *priority,
                                 uint32_t *userid,
                                 int *flags)
{
    json_t *o = NULL;

    if (!(o = json_loads (context, 0, NULL)))
        goto eproto;

    if (json_unpack (o, "{ s:i s:i s:i }",
                     "priority", priority,
                     "userid", userid,
                     "flags", flags) < 0)
        goto eproto;

    json_decref (o);
    return 0;

eproto:
    json_decref (o);
    errno = EPROTO;
    return -1;
}

int event_priority_context_decode (const char *context,
                                   int *priority)
{
    json_t *o = NULL;

    if (!(o = json_loads (context, 0, NULL)))
        goto eproto;

    if (json_unpack (o, "{ s:i }", "priority", priority) < 0)
        goto eproto;

    json_decref (o);
    return 0;

eproto:
    json_decref (o);
    errno = EPROTO;
    return -1;
}

int event_exception_context_decode (const char *context,
                                    int *severity)
{
    json_t *o = NULL;

    if (!(o = json_loads (context, 0, NULL)))
        goto eproto;

    if (json_unpack (o, "{ s:i }", "severity", severity) < 0)
        goto eproto;

    json_decref (o);
    return 0;

eproto:
    json_decref (o);
    errno = EPROTO;
    return -1;
}

int event_job_update (struct job *job, const char *event)
{
    double timestamp;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];

    if (flux_kvs_event_decode (event, &timestamp,
                               name, sizeof (name),
                               context, sizeof (context)) < 0)
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
        if (job->state != FLUX_JOB_RUN && job->state != FLUX_JOB_CLEANUP)
            goto inval;
        job->has_resources = 0;
        if (job->state == FLUX_JOB_RUN)
            job->state = FLUX_JOB_CLEANUP;
    }
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

int event_job_post (struct event_ctx *ctx, struct job *job,
                    flux_continuation_f cb, void *arg,
                    const char *name, const char *context)
{
    char key[64];
    char *event = NULL;
    int saved_errno;

    if (flux_job_kvs_key (key, sizeof (key), true, job->id, "eventlog") < 0)
        return -1;
    if (!(event = flux_kvs_event_encode (name, context)))
        return -1;
    if (event_job_update (job, event) < 0)
        goto error;
    if (event_batch_start (ctx) < 0)
        goto error;
    if (event_batch_append (ctx->batch, key, event, cb, arg) < 0)
        goto error;
    if (event_job_action (ctx, job) < 0)
        goto error;
    free (event);
    return 0;
error:
    saved_errno = errno;
    free (event);
    errno = saved_errno;
    return -1;
}

int event_job_post_pack (struct event_ctx *ctx, struct job *job,
                         flux_continuation_f cb, void *arg, const char *name,
                         const char *fmt, ...)
{
    va_list ap;
    json_t *o = NULL;
    char *context = NULL;
    int rv = -1;

    va_start (ap, fmt);
    if (!(o = json_vpack_ex (NULL, 0, fmt, ap))) {
        errno = EINVAL;
        goto error;
    }
    if (!(context = json_dumps (o, JSON_COMPACT))) {
        errno = EINVAL;
        goto error;
    }
    /* context length will be checked in event_job_post() */
    rv = event_job_post (ctx, job, cb, arg, name, context);
error:
    json_decref (o);
    free (context);
    va_end (ap);
    return rv;
}

void event_ctx_set_alloc_ctx (struct event_ctx *ctx,
                              struct alloc_ctx *alloc_ctx)
{
    ctx->alloc_ctx = alloc_ctx;
}

/* N.B. any in-flight batches are destroyed here.
 * If they are not yet fulfilled, user callbacks may synchronously block on
 * flux_future_get().
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
                event_batch_destroy (batch);
        }
        zlist_destroy (&ctx->pending);
        free (ctx);
        errno = saved_errno;
    }
}

struct event_ctx *event_ctx_create (flux_t *h)
{
    struct event_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    if (!(ctx->timer = flux_timer_watcher_create (flux_get_reactor (h),
                                                  0., 0., timer_cb, ctx)))
        goto error;
    if (!(ctx->pending = zlist_new ()))
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

