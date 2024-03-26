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
#include <jansson.h>
#include <flux/core.h>
#include <time.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libjob/idf58.h"
#include "ccan/ptrint/ptrint.h"
#include "ccan/str/str.h"

#include "alloc.h"
#include "start.h"
#include "drain.h"
#include "journal.h"
#include "wait.h"
#include "prioritize.h"
#include "annotate.h"
#include "purge.h"
#include "jobtap-internal.h"

#include "event.h"

struct event {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    double batch_timeout;
    struct event_batch *batch;
    flux_watcher_t *timer;
    zlist_t *pending;
    zhashx_t *evindex;
};

struct event_batch {
    struct event *event;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    json_t *state_trans;
    zlist_t *responses; // responses deferred until batch complete
    zlist_t *jobs;      // jobs held until batch complete
};

static struct event_batch *event_batch_create (struct event *event);
static void event_batch_destroy (struct event_batch *batch);
static int event_job_post_deferred (struct event *event, struct job *job);

/* Batch commit has completed.
 * If there was a commit error, log it and stop the reactor.
 * Destroy 'batch'.
 */
static void commit_continuation (flux_future_t *f, void *arg)
{
    struct event_batch *batch = arg;
    struct event *event = batch->event;
    struct job_manager *ctx = event->ctx;

    if (flux_future_get (batch->f, NULL) < 0) {
        flux_log_error (ctx->h, "%s: eventlog update failed", __FUNCTION__);
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
    }
    zlist_remove (event->pending, batch);
    event_batch_destroy (batch);
}

/* Close the current batch, if any, and commit it.
 */
static void event_batch_commit (struct event *event)
{
    struct event_batch *batch = event->batch;
    struct job_manager *ctx = event->ctx;

    if (batch) {
        event->batch = NULL;
        if (batch->txn) {
            if (!(batch->f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn)))
                goto error;
            if (flux_future_then (batch->f, -1., commit_continuation, batch) < 0)
                goto error;
            if (zlist_append (event->pending, batch) < 0)
                goto nomem;
        }
        else { // just send responses and be done
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

static void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct job_manager *ctx = arg;
    event_batch_commit (ctx->event);
}

/* Besides cleaning up, this function has the following side effects:
 * - send listener responses (only under error scenarios, should be
 *   sent in event_batch_commit()).
 * - respond to deferred responses (if any)
 */
static void event_batch_destroy (struct event_batch *batch)
{
    if (batch) {
        int saved_errno = errno;

        flux_kvs_txn_destroy (batch->txn);
        if (batch->f)
            (void)flux_future_wait_for (batch->f, -1);
        if (batch->jobs) {
            struct job *job;
            while ((job = zlist_pop (batch->jobs))) {
                job->hold_events = 0;
                if (event_job_post_deferred (batch->event, job) < 0)
                    flux_log_error (batch->event->ctx->h,
                                    "%s: error posting deferred events",
                                    idf58 (job->id));
            }
            zlist_destroy (&batch->jobs);
        }
        if (batch->responses) {
            flux_msg_t *msg;
            flux_t *h = batch->event->ctx->h;
            while ((msg = zlist_pop (batch->responses))) {
                if (flux_send (h, msg, 0) < 0)
                    flux_log_error (h, "error sending batch response");
                flux_msg_decref (msg);
            }
            zlist_destroy (&batch->responses);
        }
        flux_future_destroy (batch->f);
        free (batch);
        errno = saved_errno;
    }
}

static struct event_batch *event_batch_create (struct event *event)
{
    struct event_batch *batch;

    if (!(batch = calloc (1, sizeof (*batch))))
        return NULL;
    batch->event = event;
    return batch;
}

/* Create a new "batch" if there is none.
 * No-op if batch already started.
 */
static int event_batch_start (struct event *event)
{
    if (!event->batch) {
        if (!(event->batch = event_batch_create (event)))
            return -1;
        flux_timer_watcher_reset (event->timer, event->batch_timeout, 0.);
        flux_watcher_start (event->timer);
    }
    return 0;
}

static int event_batch_commit_event (struct event *event,
                                     struct job *job,
                                     json_t *entry)
{
    char key[64];
    char *entrystr = NULL;

    if (event_batch_start (event) < 0)
        return -1;
    if (flux_job_kvs_key (key, sizeof (key), job->id, "eventlog") < 0)
        return -1;
    if (!event->batch->txn && !(event->batch->txn = flux_kvs_txn_create ()))
        return -1;
    if (!(entrystr = eventlog_entry_encode (entry)))
        return -1;
    if (flux_kvs_txn_put (event->batch->txn,
                          FLUX_KVS_APPEND,
                          key,
                          entrystr) < 0) {
        free (entrystr);
        return -1;
    }
    free (entrystr);
    return 0;
}

int event_batch_add_job (struct event *event, struct job *job)
{
    if (event_batch_start (event) < 0)
        return -1;
    if (!(event->batch->jobs)) {
        if (!(event->batch->jobs = zlist_new ()))
            goto nomem;
    }
    if (zlist_append (event->batch->jobs, job) < 0)
        goto nomem;
    job->hold_events = 1;
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

int event_batch_respond (struct event *event, const flux_msg_t *msg)
{
    if (event_batch_start (event) < 0)
        return -1;
    if (!event->batch->responses) {
        if (!(event->batch->responses = zlist_new ()))
            goto nomem;
    }
    if (zlist_append (event->batch->responses,
                      (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
    return -1;
}

int event_job_action (struct event *event, struct job *job)
{
    struct job_manager *ctx = event->ctx;

    switch (job->state) {
        case FLUX_JOB_STATE_NEW:
            break;
        case FLUX_JOB_STATE_DEPEND:
            /*  Post the "depend" event when the job has no more dependency
             *   references outstanding and a depend event hasn't already
             *   been posted.
             *
             *  The job->depend_posted flag is required in the case that
             *   events are being queued and handled asynchronously, and
             *   therefore the post of the "depend" event does not immediately
             *   transition the job to the PRIORITY state.
             */
            if (job_dependency_count (job) == 0
                && !job_event_is_queued (job, "dependency-add")
                && !job->depend_posted) {
                if (event_job_post_pack (event, job, "depend", 0, NULL) < 0)
                    return -1;
                job->depend_posted = 1;
            }
            break;
        case FLUX_JOB_STATE_PRIORITY:
            /*
             * In the event we have re-entered this state from the
             * SCHED state, dequeue the job first.
             */
            if (job->alloc_pending)
                alloc_cancel_alloc_request (ctx->alloc, job);
            if (job->alloc_queued)
                alloc_dequeue_alloc_request (ctx->alloc, job);
            break;
        case FLUX_JOB_STATE_SCHED:
            if (alloc_enqueue_alloc_request (ctx->alloc, job) < 0)
                return -1;
            if (alloc_queue_recalc_pending (ctx->alloc) < 0)
                return -1;
            break;
        case FLUX_JOB_STATE_RUN:
            /* Send the start request only if prolog is not running/pending.
             */
            if (!job->perilog_active
                && !job_event_is_queued (job, "prolog-start")
                && start_send_request (ctx->start, job) < 0)
                return -1;
            break;
        case FLUX_JOB_STATE_CLEANUP:
            if (job->alloc_pending)
                alloc_cancel_alloc_request (ctx->alloc, job);
            if (job->alloc_queued)
                alloc_dequeue_alloc_request (ctx->alloc, job);

            /* N.B. start_pending indicates that the start request is still
             * expecting responses.  The final response is the 'release'
             * response with final=true.  Thus once the flag is clear,
             * it is safe to release all resources to the scheduler.
             */
            if (job->has_resources
                && !job_event_is_queued (job, "epilog-start")
                && !job->perilog_active
                && !job->alloc_bypass
                && !job->start_pending
                && !job->free_posted) {
                if (alloc_send_free_request (ctx->alloc, job) < 0)
                    return -1;
                if (event_job_post_pack (ctx->event, job, "free", 0, NULL) < 0)
                    return -1;
                job->free_posted = 1;
            }

            /* Post cleanup event when cleanup is complete.
             */
            if (!job->alloc_queued
                && !job->alloc_pending
                && !job->start_pending
                && !job->has_resources
                && !job_event_is_queued (job, "epilog-start")
                && !job->perilog_active) {

                if (event_job_post_pack (event, job, "clean", 0, NULL) < 0)
                    return -1;
            }
            break;
        case FLUX_JOB_STATE_INACTIVE:
            job->eventlog_readonly = 1;
            if ((job->flags & FLUX_JOB_WAITABLE))
                wait_notify_inactive (ctx->wait, job);
            /* Reminder: event_job_action() may be called more than once
             * for a job + state, therefore zhashx_insert() may fail here and
             * not be indicative of a problem.
             */
            if (zhashx_insert (ctx->inactive_jobs, &job->id, job) == 0) {
                (void)jobtap_call (ctx->jobtap, job, "job.inactive-add", NULL);
                if (purge_enqueue_job (ctx->purge, job) < 0) {
                    flux_log (event->ctx->h,
                              LOG_ERR,
                              "%s: error adding inactive job to purge queue",
                               idf58 (job->id));
                }
            }
            (void) jobtap_call (ctx->jobtap, job, "job.destroy", NULL);
            job_aux_destroy (job);
            zhashx_delete (ctx->active_jobs, &job->id);
            drain_check (ctx->drain);
            break;
    }
    return 0;
}

static int event_submit_context_decode (json_t *context,
                                        int *urgency,
                                        uint32_t *userid,
                                        int *flags)
{
    if (json_unpack (context,
                     "{s:i s:i s:i}",
                     "urgency", urgency,
                     "userid", userid,
                     "flags", flags) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static int event_priority_context_decode (json_t *context,
                                          int64_t *priority)
{
    if (json_unpack (context, "{ s:I }", "priority", priority) < 0) {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int event_urgency_context_decode (json_t *context,
                                         int *urgency)
{
    if (json_unpack (context, "{ s:i }", "urgency", urgency) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static int event_exception_context_decode (json_t *context,
                                           int *severity,
                                           const char **typep)
{
    const char *type;

    if (json_unpack (context,
                     "{s:i s:s}",
                     "severity", severity,
                     "type", &type) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (typep)
        *typep = type;
    return 0;
}

static int event_release_context_decode (json_t *context,
                                         int *final)
{
    *final = 0;

    if (json_unpack (context, "{ s:b }", "final", &final) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static int event_handle_dependency (struct job *job,
                                    const char *cmd,
                                    json_t *context)
{
    const char *desc;

    if (json_unpack (context, "{s:s}", "description", &desc) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (streq (cmd, "add"))
        return job_dependency_add (job, desc);
    else if (streq (cmd, "remove"))
        return job_dependency_remove (job, desc);
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

/* Apply updates to the jobspec copy held in memory by the job manager.  The
 * context object is a dictionary where the keys are period-delimited paths.
 * For example, "attributes.system.duration":3600.
 */
static int event_handle_jobspec_update (struct job *job, json_t *context)
{
    if (!job->jobspec_redacted
        || job_apply_jobspec_updates (job, context) < 0)
        return -1;
    return 0;
}

static int event_handle_set_flags (struct job *job,
                                   json_t *context)
{
    json_t *o = NULL;
    size_t index;
    json_t *value;

    if (json_unpack (context, "{s:o}", "flags", &o) < 0) {
        errno = EPROTO;
        return -1;
    }
    json_array_foreach (o, index, value) {
        if (job_flag_set (job, json_string_value (value)) < 0) {
            errno = EPROTO;
            return -1;
        }
    }
    return 0;
}

/*  Handle an prolog-* or epilog-* event
 */
static int event_handle_perilog (struct job *job,
                                 const char *cmd,
                                 json_t *context)
{
    if (streq (cmd, "start")) {
        if (job->perilog_active == UINT8_MAX) {
            errno = EOVERFLOW;
            return -1;
        }
        job->perilog_active++;
    }
    else if (streq (cmd, "finish")) {
        if (job->perilog_active > 0)
            job->perilog_active--;
    }
    else {
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int event_handle_memo (struct job *job, json_t *o)
{
    return annotations_update (job, "user", o);
}

/*  Return a callback topic string for the current job state
 *
 *   NOTE: 'job.state.new' and 'job.state.depend' are not currently used
 *    since jobs do not transition through these states in
 *    event_job_post_pack().
 */
static const char *state_topic (struct job *job)
{
    switch (job->state) {
        case FLUX_JOB_STATE_NEW:
            return "job.state.new";
        case FLUX_JOB_STATE_DEPEND:
            return "job.state.depend";
        case FLUX_JOB_STATE_PRIORITY:
            return "job.state.priority";
        case FLUX_JOB_STATE_SCHED:
            return "job.state.sched";
        case FLUX_JOB_STATE_RUN:
            return "job.state.run";
        case FLUX_JOB_STATE_CLEANUP:
            return "job.state.cleanup";
        case FLUX_JOB_STATE_INACTIVE:
            return "job.state.inactive";
    }
    /* NOTREACHED */
    return "job.state.none";
}

/* This function implements state transitions per RFC 21.
 * On a fatal exception or cleanup event, capture the event in job->end_event.
 */
int event_job_update (struct job *job, json_t *event)
{
    double timestamp;
    const char *name;
    json_t *context;

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0)
        goto error;

    if (streq (name, "submit")) { // invariant: submit is always first event
        if (job->state != FLUX_JOB_STATE_NEW)
            goto inval;
        job->t_submit = timestamp;
        if (event_submit_context_decode (context,
                                         &job->urgency,
                                         &job->userid,
                                         &job->flags) < 0)
            goto error;
    }
    else if (streq (name, "invalidate")) {
        job->eventlog_readonly = 1;
    }
    else if (streq (name, "validate")) {
        job->state = FLUX_JOB_STATE_DEPEND;
    }
    else if (streq (name, "jobspec-update")) {
        if (event_handle_jobspec_update (job, context) < 0)
            goto inval;
        /*  Transition a job in SCHED state back to PRIORITY to trigger
         *  possible recalculation of job priority, update scheduler with
         *  new jobspec, etc. Job will transition back to SCHED after a
         *  priority is assigned.
         */
        if (job->state == FLUX_JOB_STATE_SCHED)
            job->state = FLUX_JOB_STATE_PRIORITY;
    }
    else if (streq (name, "resource-update")) {
        if (job_apply_resource_updates (job, context) < 0)
            goto inval;
    }
    else if (strstarts (name, "dependency-")) {
        if (job->state == FLUX_JOB_STATE_DEPEND
            || job->state == FLUX_JOB_STATE_NEW) {
            if (event_handle_dependency (job, name+11, context) < 0)
                goto error;
        }
    }
    else if (streq (name, "set-flags")) {
        if (event_handle_set_flags (job, context) < 0)
            goto error;
    }
    else if (streq (name, "memo")) {
        if (event_handle_memo (job, context) < 0)
            goto error;
    }
    else if (streq (name, "depend")) {
        if (job->state == FLUX_JOB_STATE_DEPEND)
            job->state = FLUX_JOB_STATE_PRIORITY;
    }
    else if (streq (name, "priority")) {
        if (job->state == FLUX_JOB_STATE_PRIORITY
            || job->state == FLUX_JOB_STATE_SCHED) {
            if (event_priority_context_decode (context, &job->priority) < 0)
                goto error;
        }
        if (job->state == FLUX_JOB_STATE_PRIORITY)
            job->state = FLUX_JOB_STATE_SCHED;
    }
    else if (streq (name, "urgency")) {
        /* Update urgency value.  If in SCHED state, transition back to
         * PRIORITY state to trigger jobtap plugin to recompute priority.
         * N.B. event_job_action() takes the job out of the alloc queue,
         * if applicable.  The job will be requeued when it transitions back
         * to SCHED with a new priority value.
         */
        if (event_urgency_context_decode (context, &job->urgency) < 0)
            goto error;
        if (job->state == FLUX_JOB_STATE_SCHED)
            job->state = FLUX_JOB_STATE_PRIORITY;
    }
    else if (streq (name, "exception")) {
        int severity;
        const char *type;
        if (job->state != FLUX_JOB_STATE_INACTIVE
            && job->state != FLUX_JOB_STATE_NEW) {
            if (event_exception_context_decode (context, &severity, &type) < 0)
                goto error;
            if (severity == 0) {
                // resource allocation could not be reinstiated
                if (streq (type, "scheduler-restart")) {
                    if (job->has_resources)
                        job->has_resources = 0;
                }
                if (!job->end_event)
                    job->end_event = json_incref (event);
                job->state = FLUX_JOB_STATE_CLEANUP;
            }
        }
    }
    else if (streq (name, "alloc")) {
        job->has_resources = 1;
        if (job->state == FLUX_JOB_STATE_SCHED)
            job->state = FLUX_JOB_STATE_RUN;
    }
    else if (streq (name, "free")) {
        job->has_resources = 0;
    }
    else if (streq (name, "finish")) {
        if (job->state == FLUX_JOB_STATE_RUN) {
            if (!job->end_event)
                job->end_event = json_incref (event);
            job->state = FLUX_JOB_STATE_CLEANUP;
        }
    }
    else if (streq (name, "release")) {
        int final;
        if (event_release_context_decode (context, &final) < 0)
            goto error;
    }
    else if (streq (name, "clean")) {
        if (job->state == FLUX_JOB_STATE_CLEANUP) {
            job->state = FLUX_JOB_STATE_INACTIVE;
            job->t_clean = timestamp;
        }
    }
    else if (strstarts (name, "prolog-")) {
        if (!job->start_pending) {
            if (event_handle_perilog (job, name+7, context) < 0)
                goto error;
        }
    }
    else if (strstarts (name, "epilog-")) {
        if (job->state == FLUX_JOB_STATE_CLEANUP) {
            if (event_handle_perilog (job, name+7, context) < 0)
                goto error;
        }
    }
    else if (streq (name, "flux-restart")) {
        /* The flux-restart event is currently only posted to jobs in
         * SCHED state since that is the only state transition defined
         * for the event in RFC21.  In the future, other transitions
         * may be defined.
         */
        if (job->state == FLUX_JOB_STATE_SCHED)
            job->state = FLUX_JOB_STATE_PRIORITY;
    }
    return 0;
inval:
    errno = EINVAL;
error:
    return -1;
}

/*  Call jobtap plugin for event if necessary.
 *  Currently jobtap plugins are called only on state transitions or
 *   update of job urgency via "urgency" event.
 */
static int event_jobtap_call (struct event *event,
                              struct job *job,
                              const char *name,
                              json_t *entry,
                              flux_job_state_t old_state)
{

    /*  Notify any subscribers of all events, separately from
     *   special cases for state change and urgency events below.
     */
    if (jobtap_notify_subscribers (event->ctx->jobtap,
                                   job,
                                   name,
                                   "{s:O}",
                                   "entry", entry) < 0)
            flux_log (event->ctx->h, LOG_ERR,
                      "jobtap: event.%s callback failed for job %s",
                      name,
                      idf58 (job->id));

    /*
     *  Notify plugins not subscribed to all events of a jobspec update
     *  since this is a more common case.
     *
     *  This callback should occur before the state transition callback
     *  below, since jobspec-update will transition a job in SCHED back to
     *  PRIORITY, and plugins should be notified of the jobspec changes
     *  *before* the `job.state.priority` callback to allow for adjustment
     *  of internal state normally established before the first time the
     *  job.state.priority topic is called.
     */
    if (streq (name, "jobspec-update")) {
        json_t *updates;
        if (json_unpack (entry, "{s:o}", "context", &updates) < 0) {
            flux_log (event->ctx->h,
                      LOG_ERR,
                      "unable to unpack jobspec-update context for %s",
                      idf58 (job->id));
            return -1;
        }
        (void) jobtap_call (event->ctx->jobtap,
                            job,
                            "job.update",
                            "{s:O}",
                            "updates", updates);
    }

    if (job->state != old_state) {
        /*
         *  Call plugin callback on state change
         */
        return jobtap_call (event->ctx->jobtap,
                            job,
                            state_topic (job),
                            "{s:O s:i}",
                            "entry", entry,
                            "prev_state", old_state);
    }
    return 0;
}

static int event_job_cache (struct event *event,
                            struct job *job,
                            const char *name)
{
    int id;
    /*  Get a unique event id for event 'name' and stash it with the job */
    if ((id = event_index (event, name)) < 0)
        return -1;
    return job_event_id_set (job, id);
}

int event_job_process_entry (struct event *event,
                             struct job *job,
                             int flags,
                             json_t *entry)
{
    flux_job_state_t old_state = job->state;
    const char *name;
    json_t *context;

    if (eventlog_entry_parse (entry, NULL, &name, &context) < 0)
        return -1;

    /*  Forbid fatal exceptions in NEW state.
     */
    if (job->state == FLUX_JOB_STATE_NEW && streq (name, "exception")) {
        int severity;
        if (event_exception_context_decode (context, &severity, NULL))
            return -1;
        if (severity == 0) {
            flux_log (event->ctx->h,
                      LOG_ERR,
                      "fatal job exception was posted in NEW state");
            return -1;
        }
    }

    if (json_array_append (job->eventlog, entry) < 0) {
        errno = ENOMEM;
        return -1;
    }

    if (journal_process_event (event->ctx->journal,
                               job->id,
                               name,
                               entry) < 0)
        return -1;
    if (event_job_update (job, entry) < 0) // modifies job->state
        return -1;
    if (event_job_cache (event, job, name) < 0)
        return -1;
    if (!(flags & EVENT_NO_COMMIT)
        && event_batch_commit_event (event, job, entry) < 0)
        return -1;

    /* Keep track of running job count.
     * If queue reaches idle state, event_job_action() triggers any waiters.
     */
    if ((job->state & FLUX_JOB_STATE_RUNNING)
        && !(old_state & FLUX_JOB_STATE_RUNNING))
        event->ctx->running_jobs++;
    else if (!(job->state & FLUX_JOB_STATE_RUNNING)
        && (old_state & FLUX_JOB_STATE_RUNNING))
        event->ctx->running_jobs--;

    /*  Note: Failure from the jobtap call is currently ignored, but will
     *   be logged in jobtap_call(). The goal is to do something with the
     *   errors at some point (perhaps raise a job exception).
     */
    (void) event_jobtap_call (event, job, name, entry, old_state);

    /*  After processing a resource-update event, send the updated
     *  expiration to the job execution service.
     */
    if (streq (name, "resource-update")
        && start_send_expiration_update (event->ctx->start, job, context) < 0)
        flux_log (event->ctx->h,
                  LOG_ERR,
                  "%s: failed to send expiration update to exec service",
                  idf58 (job->id));

    return event_job_action (event, job);
}

static int event_job_post_deferred (struct event *event, struct job *job)
{
    int flags;
    json_t *entry;

    job_incref (job); // in case event_job_process_entry() decrefs job
    while (job_event_peek (job, &flags, &entry) == 0
        && !job->eventlog_readonly) {
        if (event_job_process_entry (event, job, flags, entry) < 0) {
            int saved_errno = errno;
            while (job_event_dequeue (job, NULL, NULL) == 0)
                ;
            errno = saved_errno;
            job_decref (job);
            return -1;
        }
        job_event_dequeue (job, NULL, NULL);
    }
    job_decref (job);
    return 0;
}

/* Since event_job_process_entry() might call event_job_post_*() to post
 * new events, use job->event_queue to ensure events are processed in order
 * and unnecessary recursion is avoided.
 */
int event_job_post_entry (struct event *event,
                          struct job *job,
                          int flags,
                          json_t *entry)
{
    if (job_event_enqueue (job, flags, entry) < 0)
        return -1;
    if (json_array_size (job->event_queue) > 1 || job->hold_events)
        return 0; // break recursion
    return event_job_post_deferred (event, job);
}

int event_job_post_vpack (struct event *event,
                          struct job *job,
                          const char *name,
                          int flags,
                          const char *context_fmt,
                          va_list ap)
{
    json_t *entry = NULL;
    int saved_errno;
    int rc;

    if (!(entry = eventlog_entry_vpack (0., name, context_fmt, ap)))
        return -1;
    rc = event_job_post_entry (event, job, flags, entry);

    saved_errno = errno;
    json_decref (entry);
    errno = saved_errno;
    return rc;
}

int event_job_post_pack (struct event *event,
                         struct job *job,
                         const char *name,
                         int flags,
                         const char *context_fmt,
                         ...)
{
    int rc;
    va_list ap;

    va_start (ap, context_fmt);
    rc = event_job_post_vpack (event, job, name, flags, context_fmt, ap);
    va_end (ap);
    return rc;
}

/* Finalizes in-flight batch KVS commits and event pubs (synchronously).
 */
void event_ctx_destroy (struct event *event)
{
    if (event) {
        int saved_errno = errno;
        flux_watcher_destroy (event->timer);
        flux_msg_handler_delvec (event->handlers);
        event_batch_commit (event);
        if (event->pending) {
            struct event_batch *batch;
            while ((batch = zlist_pop (event->pending)))
                event_batch_destroy (batch);
        }
        zlist_destroy (&event->pending);
        zhashx_destroy (&event->evindex);
        free (event);
        errno = saved_errno;
    }
}

static void set_timeout_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct event *event = arg;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:F}",
                             "timeout", &event->batch_timeout) < 0)
        goto error;
    if (flux_respond (h, msg, NULL) < 0)
        goto error;
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "flux_msg_respond_error");
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,
      "job-manager.set-batch-timeout",
      set_timeout_cb,
      0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct event *event_ctx_create (struct job_manager *ctx)
{
    struct event *event;

    if (!(event = calloc (1, sizeof (*event))))
        return NULL;
    event->ctx = ctx;
    event->batch_timeout = 0.01;
    if (!(event->timer = flux_timer_watcher_create (flux_get_reactor (ctx->h),
                                                    0.,
                                                    0.,
                                                    timer_cb,
                                                    ctx)))
        goto error;
    if (!(event->pending = zlist_new ()))
        goto nomem;
    if (!(event->evindex = zhashx_new ()))
        goto nomem;
    if (flux_msg_handler_addvec (ctx->h, htab, event, &event->handlers) < 0)
        goto error;

    return event;
nomem:
    errno = ENOMEM;
error:
    event_ctx_destroy (event);
    return NULL;
}

int event_index (struct event *event, const char *name)
{
    void *entry = zhashx_lookup (event->evindex, name);
    if (!entry) {
        entry = int2ptr (((int) zhashx_size (event->evindex) + 1));
        (void)zhashx_insert (event->evindex, name, entry);
    }
    return ptr2int (entry);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

