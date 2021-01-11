/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* alloc.c - scheduler interface
 *
 * Please refer to RFC27 for scheduler protocol
 *
 * TODO:
 * - implement flow control (credit based?) interface mode
 * - handle post alloc request job priority change
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>
#include <flux/schedutil.h>
#include <assert.h>

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "drain.h"
#include "annotate.h"

typedef enum {
    SCHED_SINGLE,       // only allow one outstanding sched.alloc request
    SCHED_UNLIMITED,    // send all sched.alloc requests immediately
} sched_interface_t;

struct alloc {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlistx_t *queue;
    sched_interface_t mode;
    bool ready;
    bool disable;
    char *disable_reason;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    unsigned int alloc_pending_count; // for mode=single, max of 1
    unsigned int free_pending_count;
    struct job *pending_job; // for mode=single
    char *sched_sender; // for disconnect
};

static void requeue_pending (struct alloc *alloc, struct job *job)
{
    struct job_manager *ctx = alloc->ctx;
    bool fwd = job->priority > (FLUX_JOB_PRIORITY_MAX / 2);
    bool cleared = false;

    assert (job->alloc_pending);
    assert (job->handle == NULL);
    if (!(job->handle = zlistx_insert (alloc->queue, job, fwd)))
        flux_log (ctx->h, LOG_ERR, "failed to enqueue job for scheduling");
    job->alloc_pending = 0;
    job->alloc_queued = 1;
    alloc->pending_job = NULL;
    annotations_sched_clear (job, &cleared);
    if (cleared) {
        if (event_job_post_pack (ctx->event, job, "annotations",
                                 EVENT_JOURNAL_ONLY,
                                 "{s:n}", "annotations") < 0)
            flux_log_error (ctx->h,
                            "%s: event_job_post_pack",
                            __FUNCTION__);
    }
}

/* Initiate teardown.  Clear any alloc/free requests, and clear
 * the alloc->ready flag to stop prep/check from allocating.
 */
static void interface_teardown (struct alloc *alloc, char *s, int errnum)
{
    if (alloc->ready) {
        struct job *job;
        struct job_manager *ctx = alloc->ctx;

        flux_log (ctx->h, LOG_DEBUG, "alloc: stop due to %s: %s",
                  s, flux_strerror (errnum));

        job = zhashx_first (ctx->active_jobs);
        while (job) {
            /* jobs with alloc request pending need to go back in the queue
             * so they will automatically send alloc again.
             */
            if (job->alloc_pending)
                requeue_pending (alloc, job);
            /* jobs with free request pending (much smaller window for this
             * to be true) need to be picked up again after 'ready'.
             */
            job->free_pending = 0;
            job = zhashx_next (ctx->active_jobs);
        }
        alloc->ready = false;
        alloc->alloc_pending_count = 0;
        alloc->free_pending_count = 0;
        free (alloc->sched_sender);
        alloc->sched_sender = NULL;
        drain_check (alloc->ctx->drain);
    }
}

/* Handle a sched.free response.
 */
static void free_response_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    flux_jobid_t id = 0;
    struct job *job;

    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto teardown;
    if (flux_msg_unpack (msg, "{s:I}", "id", &id) < 0)
        goto teardown;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        flux_log (h, LOG_ERR, "sched.free-response: id=%ju not active",
                  (uintmax_t)id);
        errno = EINVAL;
        goto teardown;
    }
    if (!job->has_resources) {
        flux_log (h, LOG_ERR, "sched.free-response: id=%ju not allocated",
                  (uintmax_t)id);
        errno = EINVAL;
        goto teardown;
    }
    job->free_pending = 0;
    ctx->alloc->free_pending_count--;
    if (event_job_post_pack (ctx->event, job, "free", 0, NULL) < 0)
        goto teardown;
    return;
teardown:
    interface_teardown (ctx->alloc, "free response error", errno);
}

/* Send sched.free request for job.
 * Update flags.
 */
int free_request (struct alloc *alloc, struct job *job)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("sched.free", NULL)))
        return -1;
    if (flux_msg_pack (msg, "{s:I}", "id", job->id) < 0)
        goto error;
    if (flux_send (alloc->ctx->h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

/* Send sched.cancel request for job.
*/
int cancel_request (struct alloc *alloc, struct job *job)
{
    flux_future_t *f;
    flux_t *h = alloc->ctx->h;

    if (!(f = flux_rpc_pack (h,
                             "sched.cancel",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:I}",
                             "id",
                             job->id))) {
        flux_log_error (h, "sending sched.cancel id=%ju", (uintmax_t)job->id);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

/* Handle a sched.alloc response.
 * Update flags.
 */
static void alloc_response_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    flux_jobid_t id;
    int type;
    char *note = NULL;
    json_t *annotations = NULL;
    struct job *job;
    bool cleared = false;
    json_t *tmp = NULL;

    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto teardown; // ENOSYS here if scheduler not loaded/shutting down
    if (flux_msg_unpack (msg, "{s:I s:i s?:s s?:o}",
                              "id", &id,
                              "type", &type,
                              "note", &note,
                              "annotations", &annotations) < 0)
        goto teardown;
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        flux_log (h, LOG_ERR, "sched.alloc-response: id=%ju not active",
                  (uintmax_t)id);
        errno = EINVAL;
        goto teardown;
    }
    if (!job->alloc_pending) {
        flux_log (h, LOG_ERR, "sched.alloc-response: id=%ju not requested",
                  (uintmax_t)id);
        errno = EINVAL;
        goto teardown;
    }
    switch (type) {
    case FLUX_SCHED_ALLOC_SUCCESS:
        alloc->alloc_pending_count--;
        job->alloc_pending = 0;
        if (alloc->mode == SCHED_SINGLE)
            alloc->pending_job = NULL;
        if (job->has_resources) {
            flux_log (h,
                      LOG_ERR,
                      "sched.alloc-response: id=%ju already allocated",
                      (uintmax_t)id);
            errno = EEXIST;
            goto teardown;
        }
        if (annotations_update (h, job, annotations) < 0)
            flux_log_error (h, "annotations_update: id=%ju", (uintmax_t)id);
        if (annotations) {
            if (job->annotations) {
                /* deep copy necessary for journal history, as
                 * job->annotations can be modified in future */
                if (!(tmp = json_deep_copy (job->annotations)))
                    goto nomem;
            }
            if (event_job_post_pack (ctx->event,
                                     job,
                                     "annotations",
                                     EVENT_JOURNAL_ONLY,
                                     "{s:O?}",
                                     "annotations", tmp) < 0)
                flux_log_error (ctx->h,
                                "%s: event_job_post_pack: id=%ju",
                                __FUNCTION__, (uintmax_t)id);
        }
        if (job->annotations) {
            if (event_job_post_pack (ctx->event, job, "alloc", 0,
                                     "{ s:O }",
                                     "annotations", job->annotations) < 0)
                goto teardown;
        }
        else {
            if (event_job_post_pack (ctx->event, job, "alloc", 0, NULL) < 0)
                goto teardown;
        }
        break;
    case FLUX_SCHED_ALLOC_ANNOTATE: // annotation
        if (!annotations) {
            errno = EPROTO;
            goto teardown;
        }
        if (annotations_update (h, job, annotations) < 0)
            flux_log_error (h, "annotations_update: id=%ju", (uintmax_t)id);
        if (job->annotations) {
            /* deep copy necessary for journal history, as
             * job->annotations can be modified in future */
            if (!(tmp = json_deep_copy (job->annotations)))
                goto nomem;
        }
        if (event_job_post_pack (ctx->event,
                                 job,
                                 "annotations",
                                 EVENT_JOURNAL_ONLY,
                                 "{s:O?}",
                                 "annotations", tmp) < 0)
            flux_log_error (ctx->h,
                            "%s: event_job_post_pack: id=%ju",
                            __FUNCTION__, (uintmax_t)id);
        break;
    case FLUX_SCHED_ALLOC_DENY: // error
        alloc->alloc_pending_count--;
        job->alloc_pending = 0;
        if (alloc->mode == SCHED_SINGLE)
            alloc->pending_job = NULL;
        annotations_clear (job, &cleared);
        if (cleared) {
            if (event_job_post_pack (ctx->event, job, "annotations",
                                     EVENT_JOURNAL_ONLY,
                                     "{s:n}", "annotations") < 0)
                flux_log_error (ctx->h,
                                "%s: event_job_post_pack: id=%ju",
                                __FUNCTION__, (uintmax_t)id);
        }
        if (event_job_post_pack (ctx->event, job, "exception", 0,
                                 "{ s:s s:i s:i s:s }",
                                 "type", "alloc",
                                 "severity", 0,
                                 "userid", FLUX_USERID_UNKNOWN,
                                 "note", note ? note : "") < 0)
            goto teardown;
        break;
    case FLUX_SCHED_ALLOC_CANCEL:
        alloc->alloc_pending_count--;
        if (alloc->mode == SCHED_SINGLE)
            alloc->pending_job = NULL;
        if (job->state == FLUX_JOB_STATE_SCHED)
            requeue_pending (alloc, job);
        else
            annotations_clear (job, &cleared);
        job->alloc_pending = 0;
        if (cleared) {
            if (event_job_post_pack (ctx->event, job, "annotations",
                                     EVENT_JOURNAL_ONLY,
                                     "{s:n}", "annotations") < 0)
                flux_log_error (ctx->h,
                                "%s: event_job_post_pack: id=%ju",
                                __FUNCTION__, (uintmax_t)id);
        }
        if (event_job_action (ctx->event, job) < 0) {
            flux_log_error (h,
                            "event_job_action id=%ju on alloc cancel",
                            (uintmax_t)id);
            goto teardown;
        }
        drain_check (alloc->ctx->drain);
        break;
    default:
        errno = EINVAL;
        goto teardown;
    }
    json_decref (tmp);
    return;
nomem:
    errno = ENOMEM;
teardown:
    json_decref (tmp);
    interface_teardown (alloc, "alloc response error", errno);
}

/* Send sched.alloc request for job.
 * Update flags.
 */
int alloc_request (struct alloc *alloc, struct job *job)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("sched.alloc", NULL)))
        return -1;
    if (flux_msg_pack (msg, "{s:I s:I s:i s:f s:O}",
                            "id", job->id,
                            "priority", (json_int_t)job->priority,
                            "userid", job->userid,
                            "t_submit", job->t_submit,
                            "jobspec", job->jobspec_redacted) < 0)
        goto error;
    if (flux_send (alloc->ctx->h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

/* sched-hello:
 * Scheduler obtains jobs that have resources allocated.
 */
static void hello_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct job *job;

    /* N.B. no "state" is set in struct alloc after a hello msg, so do
     * not set ctx->alloc->sched_sender in here.  Do so only in the
     * ready callback */
    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!flux_msg_is_streaming (msg)) {
        errno = EPROTO;
        goto error;
    }
    flux_log (h, LOG_DEBUG, "scheduler: hello");
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (job->has_resources) {
            if (flux_respond_pack (h, msg,
                                   "{s:I s:I s:i s:f}",
                                   "id", job->id,
                                   "priority", job->priority,
                                   "userid", job->userid,
                                   "t_submit", job->t_submit) < 0)
                goto error;
        }
        job = zhashx_next (ctx->active_jobs);
    }
    if (flux_respond_error (h, msg, ENODATA, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* sched-ready:
 * Scheduler indicates what style of alloc concurrency is requires,
 * and tells job-manager to start allocations.  job-manager tells
 * scheduler how many jobs are in the queue.
 */
static void ready_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    const char *mode;
    int count;
    struct job *job;

    if (flux_request_unpack (msg, NULL, "{s:s}", "mode", &mode) < 0)
        goto error;
    if (!strcmp (mode, "single"))
        ctx->alloc->mode = SCHED_SINGLE;
    else if (!strcmp (mode, "unlimited"))
        ctx->alloc->mode = SCHED_UNLIMITED;
    else {
        errno = EPROTO;
        goto error;
    }
    if (flux_msg_get_route_first (msg, &ctx->alloc->sched_sender) < 0) {
        flux_log_error (h, "%s: flux_msg_get_route_first", __FUNCTION__);
        goto error;
    }
    ctx->alloc->ready = true;
    flux_log (h, LOG_DEBUG, "scheduler: ready %s", mode);
    count = zlistx_size (ctx->alloc->queue);
    if (flux_respond_pack (h, msg, "{s:i}", "count", count) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    /* Restart any free requests that might have been interrupted
     * when scheduler was last unloaded.
     */
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        /* N.B. first/next are NOT deletion safe but event_job_action()
         * won't call zhashx_delete() for jobs in FLUX_JOB_STATE_CLEANUP state.
         */
        if (job->state == FLUX_JOB_STATE_CLEANUP && job->has_resources) {
            if (event_job_action (ctx->event, job) < 0)
                flux_log_error (h, "%s: event_job_action", __FUNCTION__);
        }
        job = zhashx_next (ctx->active_jobs);
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* prep:
 * Runs right before reactor calls poll(2).
 * If a job can be scheduled, start idle watcher.
 */
static void prep_cb (flux_reactor_t *r, flux_watcher_t *w,
                     int revents, void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    struct job *job;

    if (!alloc->ready || alloc->disable)
        return;
    if (alloc->mode == SCHED_SINGLE && alloc->alloc_pending_count > 0)
        return;
   /* The queue is sorted from highest to lowest priority, so if the
    * first job has urgency=HOLD (priority=MIN), all other jobs must have
    * the same priority, and no alloc requests can be sent.
    */
    if ((job = zlistx_first (alloc->queue))
        && job->urgency != FLUX_JOB_URGENCY_HOLD)
        flux_watcher_start (alloc->idle);
}

/* check:
 * Runs right after reactor calls poll(2).
 * Stop idle watcher, and send next alloc request, if available.
 */
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    struct job *job;

    flux_watcher_stop (alloc->idle);
    if (!alloc->ready || alloc->disable)
        return;
    if (alloc->mode == SCHED_SINGLE && alloc->alloc_pending_count > 0)
        return;
   /* The queue is sorted from highest to lowest priority, so if the
    * first job has urgency=HOLD (priority=MIN), all other jobs must have
    * the same priority, and no alloc requests can be sent.
    */
    if ((job = zlistx_first (alloc->queue))
        && job->urgency != FLUX_JOB_URGENCY_HOLD) {
        if (alloc_request (alloc, job) < 0) {
            flux_log_error (ctx->h, "alloc_request fatal error");
            flux_reactor_stop_error (flux_get_reactor (ctx->h));
            return;
        }
        zlistx_delete (alloc->queue, job->handle);
        job->handle = NULL;
        job->alloc_pending = 1;
        job->alloc_queued = 0;
        alloc->alloc_pending_count++;
        if (alloc->mode == SCHED_SINGLE)
            alloc->pending_job = job;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post_pack (ctx->event, job,
                                       "debug.alloc-request", 0, NULL);

    }
}

/* called from event_job_action() FLUX_JOB_STATE_CLEANUP */
int alloc_send_free_request (struct alloc *alloc, struct job *job)
{
    assert (job->state == FLUX_JOB_STATE_CLEANUP);
    if (!job->free_pending && alloc->ready) {
        if (free_request (alloc, job) < 0)
            return -1;
        job->free_pending = 1;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post_pack (alloc->ctx->event, job,
                                       "debug.free-request", 0, NULL);
        alloc->free_pending_count++;
    }
    return 0;
}

/* called from event_job_action() FLUX_JOB_STATE_SCHED */
int alloc_enqueue_alloc_request (struct alloc *alloc, struct job *job)
{
    assert (job->state == FLUX_JOB_STATE_SCHED);
    if (!job->alloc_queued
        && !job->alloc_pending
        && job->urgency != FLUX_JOB_URGENCY_HOLD) {
        bool fwd = job->priority > (FLUX_JOB_PRIORITY_MAX / 2);
        assert (job->handle == NULL);
        if (!(job->handle = zlistx_insert (alloc->queue, job, fwd)))
            return -1;
        job->alloc_queued = 1;
    }
    return 0;
}

/* called from event_job_action() FLUX_JOB_STATE_CLEANUP
 * or transition from FLUX_JOB_STATE_SCHED back to FLUX_JOB_STATE_PRIORITY.
 */
void alloc_dequeue_alloc_request (struct alloc *alloc, struct job *job)
{
    if (job->alloc_queued) {
        zlistx_delete (alloc->queue, job->handle);
        job->handle = NULL;
        job->alloc_queued = 0;
    }
}

/* called from event_job_action() FLUX_JOB_STATE_CLEANUP
 * or alloc_queue_recalc_pending() if queue order has changed.
 */
int alloc_cancel_alloc_request (struct alloc *alloc, struct job *job)
{
    if (job->alloc_pending) {
        if (cancel_request (alloc, job) < 0)
            return -1;
    }
    return 0;
}

/* called from list_handle_request() */
struct job *alloc_queue_first (struct alloc *alloc)
{
    return zlistx_first (alloc->queue);
}

struct job *alloc_queue_next (struct alloc *alloc)
{
    return zlistx_next (alloc->queue);
}

/* called from urgency_handle_request() */
void alloc_queue_reorder (struct alloc *alloc, struct job *job)
{
    bool fwd = job->priority > (FLUX_JOB_PRIORITY_MAX / 2);

    zlistx_reorder (alloc->queue, job->handle, fwd);
}

/* called if highest priority job may have changed */
int alloc_queue_recalc_pending (struct alloc *alloc) {
    struct job *head = zlistx_first (alloc->queue);
    if (alloc->mode == SCHED_SINGLE
        && alloc->pending_job
        && head) {
        if (job_comparator (head, alloc->pending_job) < 0) {
            if (alloc_cancel_alloc_request (alloc, alloc->pending_job) < 0) {
                flux_log_error (alloc->ctx->h, "%s: alloc_cancel_alloc_request",
                                __FUNCTION__);
                return -1;
            }
        }
    }
    return 0;
}

int alloc_pending_count (struct alloc *alloc)
{
    return alloc->alloc_pending_count;
}

/* Cancel all pending alloc requests in preparation for disabling
 * resource allocation.
 */
static void cancel_all_pending (struct alloc *alloc)
{
    if (alloc->alloc_pending_count > 0) {
        struct job *job;

        job = zhashx_first (alloc->ctx->active_jobs);
        while (job) {
            if (job->alloc_pending)
                cancel_request (alloc, job);
            job = zhashx_next (alloc->ctx->active_jobs);
        }
    }
}

/* Control resource allocation (query/start/stop).
 * If 'query_only' is true, report allocaction status without altering it.
 * Otherwise update the alloc->disable flag, and for disable only,
 * optionally set alloc->disable_reason.
 *
 * What it means to be administratively disabled:
 * While allocation is disabled, the scheduler can remain loaded and handle
 * requests, but the job manager won't send any more allocation requests.
 * Pending alloc requests are canceled (jobs remain in SCHED state and
 * return to alloc->queue).  The job manager continues to send free requests
 * to the scheduler as jobs relinquish resources.
 *
 * If allocation is adminstratively enabled, but the scheduler is not loaded,
 * the current state is reported as disabled with reason "Scheduler is offline".
 */
static void alloc_admin_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    int query_only;
    int enable;
    const char *reason = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s:b s?:s}",
                             "query_only",
                             &query_only,
                             "enable",
                             &enable,
                             "reason",
                             &reason) < 0)
        goto error;
    if (!query_only) {
        if (!enable) {
            char *cpy = NULL;
            if (reason && strlen (reason) > 0 && !(cpy = strdup (reason)))
                goto error;
            free (alloc->disable_reason);
            alloc->disable_reason = cpy;
            cancel_all_pending (alloc);
        }
        alloc->disable = enable ? false : true;
    }
    if (alloc->disable) { // administratively disabled
        enable = 0;
        reason  = alloc->disable_reason;
    }
    else if (!alloc->ready) { // scheduler not loaded (waiting for hello)
        enable = 0;
        reason = "Scheduler is offline";
    }
    else { // condtion normal
        enable = 1;
        reason = NULL;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:b s:s s:i s:i s:i s:i}",
                           "enable",
                           enable,
                           "reason",
                           reason ? reason : "",
                           "queue_length",
                           zlistx_size (alloc->queue),
                           "alloc_pending",
                           alloc->alloc_pending_count,
                           "free_pending",
                           alloc->free_pending_count,
                           "running",
                           alloc->ctx->running_jobs) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void alloc_disconnect_rpc (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;

    if (alloc->sched_sender) {
        char *sender = NULL;
        if (flux_msg_get_route_first (msg, &sender) == 0
            && !strcmp (sender, alloc->sched_sender))
            interface_teardown (ctx->alloc, "disconnect", 0);
        free (sender);
    }
}

void alloc_ctx_destroy (struct alloc *alloc)
{
    if (alloc) {
        int saved_errno = errno;;
        flux_msg_handler_delvec (alloc->handlers);
        flux_watcher_destroy (alloc->prep);
        flux_watcher_destroy (alloc->check);
        flux_watcher_destroy (alloc->idle);
        zlistx_destroy (&alloc->queue);
        free (alloc->disable_reason);
        free (alloc->sched_sender);
        free (alloc);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "job-manager.sched-hello", hello_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "job-manager.sched-ready", ready_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "job-manager.alloc-admin", alloc_admin_cb, 0},
    { FLUX_MSGTYPE_RESPONSE, "sched.alloc", alloc_response_cb, 0},
    { FLUX_MSGTYPE_RESPONSE, "sched.free", free_response_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct alloc *alloc_ctx_create (struct job_manager *ctx)
{
    struct alloc *alloc;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (!(alloc = calloc (1, sizeof (*alloc))))
        return NULL;
    alloc->ctx = ctx;
    if (!(alloc->queue = zlistx_new()))
        goto error;
    zlistx_set_destructor (alloc->queue, job_destructor);
    zlistx_set_comparator (alloc->queue, job_comparator);
    zlistx_set_duplicator (alloc->queue, job_duplicator);

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &alloc->handlers) < 0)
        goto error;
    alloc->prep = flux_prepare_watcher_create (r, prep_cb, ctx);
    alloc->check = flux_check_watcher_create (r, check_cb, ctx);
    alloc->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!alloc->prep || !alloc->check || !alloc->idle) {
        errno = ENOMEM;
        goto error;
    }
    flux_watcher_start (alloc->prep);
    flux_watcher_start (alloc->check);
    return alloc;
error:
    alloc_ctx_destroy (alloc);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
