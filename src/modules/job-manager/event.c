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
#include "drain.h"
#include "wait.h"

#include "event.h"

#include "src/common/libeventlog/eventlog.h"

const double batch_timeout = 0.01;

struct event {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    struct event_batch *batch;
    flux_watcher_t *timer;
    zlist_t *pending;
    zlist_t *pub_futures;
    zlist_t *listeners;
};

struct event_batch {
    struct event *event;
    flux_kvs_txn_t *txn;
    flux_future_t *f;
    json_t *state_trans;
    bool listener_response_available;
    zlist_t *responses; // responses deferred until batch complete
};

struct events_listener {
    const flux_msg_t *request;
    json_t *allow;
    json_t *deny;
    json_t *events;
};

static struct event_batch *event_batch_create (struct event *event);
static void event_batch_destroy (struct event_batch *batch);

static void send_listener_responses (struct job_manager *ctx,
                                     zlist_t *listeners)
{
    struct events_listener *el;
    el = zlist_first (listeners);
    while (el) {
        if (json_array_size (el->events) > 0) {
            if (flux_respond_pack (ctx->h, el->request,
                                   "{s:O}", "events", el->events) < 0)
                flux_log_error (ctx->h, "%s: flux_respond_pack",
                                __FUNCTION__);
            else
                json_array_clear (el->events);
        }
        el = zlist_next (listeners);
    }
}

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

/* job-state event publish has completed.
 * If there was a publish error, log it and stop the reactor.
 * Destroy 'f'.
 */
static void publish_continuation (flux_future_t *f, void *arg)
{
    struct event *event = arg;
    struct job_manager *ctx = event->ctx;

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (ctx->h, "%s: event publish failed", __FUNCTION__);
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
    }
    zlist_remove (event->pub_futures, f);
    flux_future_destroy (f);
}

/* Close the current batch, if any, and commit it.
 */
static void event_batch_commit (struct event *event)
{
    struct event_batch *batch = event->batch;
    struct job_manager *ctx = event->ctx;

    if (batch) {
        event->batch = NULL;
        /* send listener responses now, do not wait on the KVS commit
         * and send before any additional events can be added onto
         * listeners via event_batch_process_event_entry()
         *
         * note that job-state events will be sent after the KVS
         * commit, as we want to ensure anyone who receives a
         * job-state transition event will be able to read the
         * corresponding event in the KVS.  The event stream does not
         * have such a requirement, since we're sending the listener
         * the data.
         */
        if (batch->listener_response_available) {
            send_listener_responses (ctx, event->listeners);
            batch->listener_response_available = false;
        }
        if (batch->txn) {
            if (!(batch->f = flux_kvs_commit (ctx->h, NULL, 0, batch->txn)))
                goto error;
            if (flux_future_then (batch->f, -1., commit_continuation, batch) < 0)
                goto error;
            if (zlist_append (event->pending, batch) < 0)
                goto nomem;
        }
        else { // just publish events & send responses and be done
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

void event_publish (struct event *event, const char *topic,
                    const char *key, json_t *o)
{
    struct job_manager *ctx = event->ctx;
    flux_future_t *f;

    if (!(f = flux_event_publish_pack (ctx->h, topic, 0, "{s:O?}", key, o))) {
        flux_log_error (ctx->h, "%s: flux_event_publish_pack", __FUNCTION__);
        goto error;
    }
    if (flux_future_then (f, -1., publish_continuation, event) < 0) {
        flux_future_destroy (f);
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }
    if (zlist_append (event->pub_futures, f) < 0) {
        flux_future_destroy (f);
        flux_log_error (ctx->h, "%s: zlist_append", __FUNCTION__);
        goto error;
    }
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (ctx->h));
}

/* Besides cleaning up, this function has the following side effects:
 * - publish state transition event (if any)
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
        if (batch->state_trans) {
            if (json_array_size (batch->state_trans) > 0)
                event_publish (batch->event,
                               "job-state",
                               "transitions",
                               batch->state_trans);
            json_decref (batch->state_trans);
        }
        /* under non-error scenarios, listener responses should be
         * sent in event_batch_commit() */
        if (batch->listener_response_available)
            send_listener_responses (batch->event->ctx,
                                     batch->event->listeners);
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
        flux_timer_watcher_reset (event->timer, batch_timeout, 0.);
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

int event_batch_pub_state (struct event *event, struct job *job,
                           double timestamp)
{
    json_t *o;

    if (event_batch_start (event) < 0)
        goto error;
    if (!event->batch->state_trans) {
        if (!(event->batch->state_trans = json_array ()))
            goto nomem;
    }
    if (!(o = json_pack ("[I,s,f]",
                         job->id,
                         flux_job_statetostr (job->state, false),
                         timestamp)))
        goto nomem;
    if (json_array_append_new (event->batch->state_trans, o)) {
        json_decref (o);
        goto nomem;
    }
    return 0;
nomem:
    errno = ENOMEM;
error:
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
        case FLUX_JOB_NEW:
            break;
        case FLUX_JOB_DEPEND:
            if (event_job_post_pack (event, job, "depend", 0, NULL) < 0)
                return -1;
            break;
        case FLUX_JOB_SCHED:
            if (alloc_enqueue_alloc_request (ctx->alloc, job) < 0)
                return -1;
            break;
        case FLUX_JOB_RUN:
            if (start_send_request (ctx->start, job) < 0)
                return -1;
            break;
        case FLUX_JOB_CLEANUP:
            if (job->alloc_pending)
                alloc_cancel_alloc_request (ctx->alloc, job);
            if (job->alloc_queued)
                alloc_dequeue_alloc_request (ctx->alloc, job);

            /* N.B. start_pending indicates that the start request is still
             * expecting responses.  The final response is the 'release'
             * response with final=true.  Thus once the flag is clear,
             * it is safe to release all resources to the scheduler.
             */
            if (job->has_resources && !job->start_pending
                                   && !job->free_pending) {
                if (alloc_send_free_request (ctx->alloc, job) < 0)
                    return -1;
            }
            /* Post cleanup event when cleanup is complete.
             */
            if (!job->alloc_queued && !job->alloc_pending
                                   && !job->free_pending
                                   && !job->start_pending
                                   && !job->has_resources) {

                if (event_job_post_pack (event, job, "clean", 0, NULL) < 0)
                    return -1;
            }
            break;
        case FLUX_JOB_INACTIVE:
            if ((job->flags & FLUX_JOB_WAITABLE))
                wait_notify_inactive (ctx->wait, job);
            zhashx_delete (ctx->active_jobs, &job->id);
            drain_check (ctx->drain);
            break;
    }
    return 0;
}

static int event_submit_context_decode (json_t *context,
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

static int event_priority_context_decode (json_t *context,
                                          int *priority)
{
    if (json_unpack (context, "{ s:i }", "priority", priority) < 0) {
        errno = EPROTO;
        return -1;
    }

    return 0;
}

static int event_exception_context_decode (json_t *context,
                                           int *severity)
{
    if (json_unpack (context, "{ s:i }", "severity", severity) < 0) {
        errno = EPROTO;
        return -1;
    }

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

/* This function implements state transitions per RFC 21.
 * If FLUX_JOB_WAITABLE flag is set, then on a fatal exception or
 * cleanup event, capture the event in job->end_event for flux_job_wait().
 */
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
        if (severity == 0) {
            if ((job->flags & FLUX_JOB_WAITABLE) && !job->end_event)
                job->end_event = json_incref (event);

            job->state = FLUX_JOB_CLEANUP;
        }
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
        if (job->state == FLUX_JOB_RUN) {
            if ((job->flags & FLUX_JOB_WAITABLE) && !job->end_event)
                job->end_event = json_incref (event);

            job->state = FLUX_JOB_CLEANUP;
        }
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

static int get_timestamp_now (double *timestamp)
{
    struct timespec ts;
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0)
        return -1;
    *timestamp = (1E-9 * ts.tv_nsec) + ts.tv_sec;
    return 0;
}

/* we need to send the job id along with each eventlog entry, so wrap
 * the eventlog entry in another object with the job id
 */
static json_t *wrap_events_entry (struct job *job, json_t *entry)
{
    json_t *wrapped_entry;
    if (!(wrapped_entry = json_pack ("{s:I s:O}",
                                     "id", job->id,
                                     "entry", entry))) {
        errno = ENOMEM;
        return NULL;
    }
    return wrapped_entry;
}

static bool allow_deny_check (struct events_listener *el, const char *name)
{
    bool add_entry = true;

    if (el->allow) {
        add_entry = false;
        if (json_object_get (el->allow, name))
            add_entry = true;
    }

    if (add_entry && el->deny) {
        if (json_object_get (el->deny, name))
            add_entry = false;
    }

    return add_entry;
}

int event_batch_process_event_entry (struct event *event,
                                     struct job *job,
                                     const char *name,
                                     json_t *entry)
{
    struct events_listener *el;
    json_t *wrapped_entry = NULL;
    int saved_errno;

    if (event_batch_start (event) < 0)
        goto error;

    el = zlist_first (event->listeners);
    while (el) {
        if (allow_deny_check (el, name)) {
            if (!wrapped_entry) {
                if (!(wrapped_entry = wrap_events_entry (job, entry)))
                    goto error;
            }
            if (json_array_append (el->events, wrapped_entry) < 0)
                goto nomem;
            event->batch->listener_response_available = true;
        }

        el = zlist_next (event->listeners);
    }
    json_decref (wrapped_entry);
    return 0;

nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (wrapped_entry);
    errno = saved_errno;
    return -1;
}

int event_job_post_pack (struct event *event,
                         struct job *job,
                         const char *name,
                         int flags,
                         const char *context_fmt,
                         ...)
{
    va_list ap;
    json_t *entry = NULL;
    int saved_errno;
    double timestamp;
    flux_job_state_t old_state = job->state;

    va_start (ap, context_fmt);
    if (get_timestamp_now (&timestamp) < 0)
        goto error;
    if (!(entry = eventlog_entry_vpack (timestamp, name, context_fmt, ap)))
        return -1;
    if (event_batch_process_event_entry (event, job, name, entry) < 0)
        goto error;
    if (EVENT_INFO_ONLY & flags)
        goto out;
    if (event_job_update (job, entry) < 0) // modifies job->state
        goto error;
    if (event_batch_commit_event (event, job, entry) < 0)
        goto error;
    if (job->state != old_state) {
        if (event_batch_pub_state (event, job, timestamp) < 0)
            goto error;
    }

    /* Keep track of running job count.
     * If queue reaches idle state, event_job_action() triggers any waiters.
     */
    if ((job->state & FLUX_JOB_RUNNING) && !(old_state & FLUX_JOB_RUNNING))
        event->ctx->running_jobs++;
    else if (!(job->state & FLUX_JOB_RUNNING) && (old_state & FLUX_JOB_RUNNING))
        event->ctx->running_jobs--;

    if (event_job_action (event, job) < 0)
        goto error;

out:
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

static void events_listener_destroy (void *data)
{
    struct events_listener *el = (struct events_listener *)data;
    if (el) {
        int saved_errno = errno;
        flux_msg_decref (el->request);
        json_decref (el->allow);
        json_decref (el->deny);
        json_decref (el->events);
        free (el);
        errno = saved_errno;
    }
}

static struct events_listener *events_listener_create (const flux_msg_t *msg,
                                                       json_t *allow,
                                                       json_t *deny)
{
    struct events_listener *el;

    if (!(el = calloc (1, sizeof (*el))))
        goto error;
    el->request = flux_msg_incref (msg);
    el->allow = json_incref (allow);
    el->deny = json_incref (deny);
    if (!(el->events = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    return el;
error:
    events_listener_destroy (el);
    return NULL;
}

static void events_handle_request (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct job_manager *ctx = arg;
    struct event *event = ctx->event;
    struct events_listener *el = NULL;
    const char *errstr = NULL;
    json_t *allow = NULL;
    json_t *deny = NULL;

    if (flux_request_unpack (msg, NULL, "{s?o s?o}",
                             "allow", &allow,
                             "deny", &deny) < 0)
        goto error;

    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        errstr = "job-manager.events requires streaming RPC flag";
        goto error;
    }

    if (allow && !json_is_object (allow)) {
        errno = EPROTO;
        errstr = "job-manager.events allow should be an object";
        goto error;
    }

    if (deny && !json_is_object (deny)) {
        errno = EPROTO;
        errstr = "job-manager.events deny should be an object";
        goto error;
    }

    if (!(el = events_listener_create (msg, allow, deny)))
        goto error;

    if (zlist_append (event->listeners, el) < 0) {
        errno = ENOMEM;
        goto error;
    }
    zlist_freefn (event->listeners, el, events_listener_destroy, true);

    return;

error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    events_listener_destroy (el);
}

static bool match_events_listener (struct events_listener *el,
                                   uint32_t matchtag,
                                   const char *sender)
{
    uint32_t t;
    char *s = NULL;
    bool found = false;

    if (!flux_msg_get_matchtag (el->request, &t)
        && matchtag == t
        && !flux_msg_get_route_first (el->request, &s)
        && !strcmp (sender, s))
        found = true;
    free (s);
    return found;
}

static void events_cancel_request (flux_t *h, flux_msg_handler_t *mh,
                                   const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct event *event = ctx->event;
    struct events_listener *el;
    uint32_t matchtag;
    char *sender = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i}", "matchtag", &matchtag) < 0
        || flux_msg_get_route_first (msg, &sender) < 0) {
        flux_log_error (h, "error decoding events-cancel request");
        return;
    }
    el = zlist_first (event->listeners);
    while (el) {
        if (match_events_listener (el, matchtag, sender))
            break;
        el = zlist_next (event->listeners);
    }
    if (el) {
        if (flux_respond_error (h, el->request, ENODATA, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        zlist_remove (event->listeners, el);
    }
    free (sender);
}

static int create_zlist_and_append (zlist_t **lp, void *item)
{
    if (!*lp && !(*lp = zlist_new ())) {
        errno = ENOMEM;
        return -1;
    }
    if (zlist_append (*lp, item) < 0) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void event_listeners_disconnect_rpc (flux_t *h,
                                     flux_msg_handler_t *mh,
                                     const flux_msg_t *msg,
                                     void *arg)
{
    struct job_manager *ctx = arg;
    struct event *event = ctx->event;
    struct events_listener *el;
    char *sender;
    zlist_t *tmplist = NULL;

    if (flux_msg_get_route_first (msg, &sender) < 0)
        return;
    el = zlist_first (event->listeners);
    while (el) {
        char *tmpsender;
        if (flux_msg_get_route_first (el->request, &tmpsender) == 0) {
            if (!strcmp (sender, tmpsender)) {
                /* cannot remove from zlist while iterating, so we
                 * store off entries to remove on another list */
                if (create_zlist_and_append (&tmplist, el) < 0) {
                    flux_log_error (h, "job-manager.disconnect: "
                                    "failed to remove event listener");
                    free (tmpsender);
                    goto error;
                }
            }
            free (tmpsender);
        }
        el = zlist_next (event->listeners);
    }
    if (tmplist) {
        while ((el = zlist_pop (tmplist)))
            zlist_remove (event->listeners, el);
    }
    free (sender);
error:
    zlist_destroy (&tmplist);
}

/* Finalizes in-flight batch KVS commits and event pubs (synchronously).
 */
void event_ctx_destroy (struct event *event)
{
    if (event) {
        int saved_errno = errno;
        flux_msg_handler_delvec (event->handlers);
        flux_watcher_destroy (event->timer);
        event_batch_commit (event);
        if (event->pending) {
            struct event_batch *batch;
            while ((batch = zlist_pop (event->pending)))
                event_batch_destroy (batch); // N.B. can append to pub_futures
        }
        zlist_destroy (&event->pending);
        if (event->pub_futures) {
            flux_future_t *f;
            while ((f = zlist_pop (event->pub_futures))) {
                if (flux_future_get (f, NULL) < 0)
                    flux_log_error (event->ctx->h,
                                    "error publishing job-state event");
                flux_future_destroy (f);
            }
        }
        zlist_destroy (&event->pub_futures);
        if (event->listeners) {
            struct events_listener *el;
            while ((el = zlist_pop (event->listeners))) {
                if (flux_respond_error (event->ctx->h,
                                        el->request,
                                        ENODATA, NULL) < 0)
                    flux_log_error (event->ctx->h, "%s: flux_respond_error",
                                    __FUNCTION__);
                events_listener_destroy (el);
            }
            zlist_destroy (&event->listeners);
        }
        free (event);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.events",
        events_handle_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.events-cancel",
        events_cancel_request,
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
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &event->handlers) < 0)
        goto error;
    if (!(event->timer = flux_timer_watcher_create (flux_get_reactor (ctx->h),
                                                    0.,
                                                    0.,
                                                    timer_cb,
                                                    ctx)))
        goto error;
    if (!(event->pending = zlist_new ()))
        goto nomem;
    if (!(event->pub_futures = zlist_new ()))
        goto nomem;
    if (!(event->listeners = zlist_new ()))
        goto nomem;
    return event;
nomem:
    errno = ENOMEM;
error:
    event_ctx_destroy (event);
    return NULL;
}

int event_listeners_count (struct event *event)
{
    if (event)
        return zlist_size (event->listeners);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

