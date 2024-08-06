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
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <flux/schedutil.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/idf58.h"
#include "src/common/librlist/rlist.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "drain.h"
#include "annotate.h"
#include "raise.h"
#include "queue.h"
#include "housekeeping.h"

struct alloc {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlistx_t *queue;
    zlistx_t *sent;             // track jobs w/ alloc reqs, mode=limited only
    bool scheduler_is_online;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    unsigned int alloc_limit;   // will have a value of 0 in mode=unlimited
    char *sched_sender;         // scheduler uuid for disconnect processing
};

static void requeue_pending (struct alloc *alloc, struct job *job)
{
    struct job_manager *ctx = alloc->ctx;

    if (!job->alloc_pending)
        return;
    if (job_priority_queue_delete (alloc->sent, job) < 0)
        flux_log (ctx->h, LOG_ERR, "failed to dequeue pending job");
    job->alloc_pending = 0;
    if (queue_started (alloc->ctx->queue, job)) {
        if (job_priority_queue_insert (alloc->queue, job) < 0)
            flux_log (ctx->h, LOG_ERR, "failed to enqueue job for scheduling");
        job->alloc_queued = 1;
    }
    annotations_clear_and_publish (ctx, job, "sched");
}

/* Initiate teardown.  Clear any alloc/free requests, and clear
 * the alloc->scheduler_is_online flag to stop prep/check from allocating.
 */
static void interface_teardown (struct alloc *alloc, char *s, int errnum)
{
    if (alloc->scheduler_is_online) {
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
            job = zhashx_next (ctx->active_jobs);
        }
        alloc->scheduler_is_online = false;
        free (alloc->sched_sender);
        alloc->sched_sender = NULL;
        drain_check (alloc->ctx->drain);
    }
}

/* Send sched.free request.
 */
int free_request (struct alloc *alloc,
                  flux_jobid_t id,
                  json_t *R,
                  bool final)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("sched.free", NULL)))
        return -1;
    if (flux_msg_pack (msg,
                       "{s:I s:O s:b}",
                       "id", id,
                       "R", R,
                       "final", final) < 0)
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
                             "id", job->id))) {
        flux_log_error (h, "sending sched.cancel id=%s", idf58 (job->id));
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

/* Handle a sched.alloc response.
 * Update flags.
 */
static void alloc_response_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    flux_jobid_t id;
    int type;
    char *note = NULL;
    json_t *annotations = NULL;
    json_t *R = NULL;
    struct job *job;

    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto teardown; // ENOSYS here if scheduler not loaded/shutting down
    if (flux_msg_unpack (msg,
                         "{s:I s:i s?s s?o s?o}",
                         "id", &id,
                         "type", &type,
                         "note", &note,
                         "annotations", &annotations,
                         "R", &R) < 0)
        goto teardown;

    job = zhashx_lookup (ctx->active_jobs, &id);
    if (job && !job->alloc_pending)
        job = NULL;

    switch (type) {
    case FLUX_SCHED_ALLOC_SUCCESS:
        if (!R) {
            flux_log (h, LOG_ERR, "sched.alloc-response: protocol error");
            errno = EPROTO;
            goto teardown;
        }
        (void)json_object_del (R, "scheduling");

        if (!job) {
            (void)free_request (alloc, id, R, true);
            break;
        }
        if (job_priority_queue_delete (alloc->sent, job) < 0)
            flux_log (ctx->h, LOG_ERR, "failed to dequeue pending job");
        if (job->has_resources || job->R_redacted) {
            flux_log (h,
                      LOG_ERR,
                      "sched.alloc-response: id=%s already allocated",
                      idf58 (id));
            errno = EEXIST;
            goto teardown;
        }
        job->R_redacted = json_incref (R);
        if (annotations_update_and_publish (ctx, job, annotations) < 0)
            flux_log_error (h, "annotations_update: id=%s", idf58 (id));

        /*  Only modify job state after annotation event is published
         */
        job->alloc_pending = 0;
        if (job->annotations) {
            if (event_job_post_pack (ctx->event,
                                     job,
                                     "alloc",
                                     0,
                                     "{s:O}",
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
        if (!job)
            break;
        if (annotations_update_and_publish (ctx, job, annotations) < 0)
            flux_log_error (h, "annotations_update: id=%s", idf58 (id));
        break;
    case FLUX_SCHED_ALLOC_DENY: // error
        if (!job)
            break;
        job->alloc_pending = 0;
        if (job_priority_queue_delete (alloc->sent, job) < 0)
            flux_log (ctx->h, LOG_ERR, "failed to dequeue pending job");
        annotations_clear_and_publish (ctx, job, NULL);
        if (raise_job_exception (ctx,
                                 job,
                                 "alloc",
                                 0,
                                 ctx->owner,
                                 note) < 0)
            goto teardown;
        break;
    case FLUX_SCHED_ALLOC_CANCEL:
        if (!job)
            break;
        if (job->state == FLUX_JOB_STATE_SCHED)
            requeue_pending (alloc, job);
        else {
            if (job_priority_queue_delete (alloc->sent, job) < 0)
                flux_log (ctx->h, LOG_ERR, "failed to dequeue pending job");
            annotations_clear_and_publish (ctx, job, NULL);
        }
        job->alloc_pending = 0;
        if (queue_started (alloc->ctx->queue, job)) {
            if (event_job_action (ctx->event, job) < 0) {
                flux_log_error (h,
                                "event_job_action id=%s on alloc cancel",
                                idf58 (id));
                goto teardown;
            }
        }
        drain_check (alloc->ctx->drain);
        break;
    default:
        errno = EINVAL;
        goto teardown;
    }
    return;
teardown:
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
    if (flux_msg_pack (msg,
                       "{s:I s:I s:I s:f s:O}",
                       "id", job->id,
                       "priority", (json_int_t)job->priority,
                       "userid", (json_int_t) job->userid,
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
static void hello_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
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
        if (job->has_resources && !job->alloc_bypass) {
            if (flux_respond_pack (h,
                                   msg,
                                   "{s:I s:I s:I s:f}",
                                   "id", job->id,
                                   "priority", job->priority,
                                   "userid", (json_int_t) job->userid,
                                   "t_submit", job->t_submit) < 0)
                goto error;
        }
        job = zhashx_next (ctx->active_jobs);
    }
    if (housekeeping_hello_respond (ctx->housekeeping, msg) < 0)
        goto error;
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
static void ready_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_manager *ctx = arg;
    const char *mode;
    int limit = 0;
    int count;
    struct job *job;
    const char *sender;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s?i}",
                             "mode", &mode,
                             "limit", &limit) < 0)
        goto error;
    if (streq (mode, "limited")) {
        if (limit <= 0) {
            errno = EPROTO;
            goto error;
        }
        ctx->alloc->alloc_limit = limit;
    }
    else if (streq (mode, "unlimited"))
        ctx->alloc->alloc_limit = 0;
    else {
        errno = EPROTO;
        goto error;
    }
    if (!(sender = flux_msg_route_first (msg))) {
        flux_log (h, LOG_ERR, "%s: flux_msg_get_route_first: sender is NULL",
                  __FUNCTION__);
        goto error;
    }
    if (sender) {
        if (!(ctx->alloc->sched_sender = strdup (sender)))
            goto error;
    }
    ctx->alloc->scheduler_is_online = true;
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


static bool alloc_work_available (struct job_manager *ctx)
{
    struct job *job;

    if (!ctx->alloc->scheduler_is_online) // scheduler is not ready for alloc
        return false;
    if (!(job = zlistx_first (ctx->alloc->queue))) // queue is empty
        return false;
    if (ctx->alloc->alloc_limit > 0 // alloc limit reached
        && zlistx_size (ctx->alloc->sent) >= ctx->alloc->alloc_limit)
        return false;
    /* The alloc->queue is sorted from highest to lowest priority, so if the
     * first job has priority=MIN (held), all other jobs must have the same
     * priority, and no alloc requests can be sent.
     */
    if (job->priority == FLUX_JOB_PRIORITY_MIN)
        return false;
    return true;
}

/* prep:
 * Runs right before reactor calls poll(2).
 * If a job can be scheduled, start idle watcher.
 */
static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct job_manager *ctx = arg;

    if (alloc_work_available (ctx))
        flux_watcher_start (ctx->alloc->idle);
}

/* check:
 * Runs right after reactor calls poll(2).
 * Stop idle watcher, and send next alloc request, if available.
 */
static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    struct job *job;

    flux_watcher_stop (alloc->idle);

    if (!alloc_work_available (ctx))
        return;

    job = zlistx_first (alloc->queue);

    if (alloc_request (alloc, job) < 0) {
        flux_log_error (ctx->h, "alloc_request fatal error");
        flux_reactor_stop_error (flux_get_reactor (ctx->h));
        return;
    }
    job_priority_queue_delete (alloc->queue, job);
    job->alloc_pending = 1;
    job->alloc_queued = 0;
    if (job_priority_queue_insert (alloc->sent, job) < 0)
        flux_log (ctx->h, LOG_ERR, "failed to enqueue pending job");
    /* Post event for debugging if job was submitted FLUX_JOB_DEBUG flag.
     */
    if ((job->flags & FLUX_JOB_DEBUG))
        (void)event_job_post_pack (ctx->event,
                                   job,
                                   "debug.alloc-request",
                                   0,
                                   NULL);
}

int alloc_send_free_request (struct alloc *alloc,
                             json_t *R,
                             flux_jobid_t id,
                             bool final)
{
    if (alloc->scheduler_is_online) {
        if (free_request (alloc, id, R, final) < 0)
            return -1;
    }
    return 0;
}

/* called from event_job_action() FLUX_JOB_STATE_SCHED */
int alloc_enqueue_alloc_request (struct alloc *alloc, struct job *job)
{
    if (job->state != FLUX_JOB_STATE_SCHED)
        return -1;
    if (!job->alloc_bypass
        && !job->alloc_queued
        && !job->alloc_pending
        && job->priority != FLUX_JOB_PRIORITY_MIN
        && queue_started (alloc->ctx->queue, job)) {
        if (job_priority_queue_insert (alloc->queue, job) < 0)
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
        job_priority_queue_delete (alloc->queue, job);
        job->alloc_queued = 0;
    }
}

/* Send a sched.cancel request for job.  This RPC receives no direct response.
 * Instead, the sched.alloc request receives a FLUX_SCHED_ALLOC_CANCEL or a
 * FLUX_SCHED_ALLOC_SUCCESS response.
 *
 * As described in RFC 27, sched.alloc requests are canceled when:
 * 1) a job in SCHED state is canceled
 * 2) a queue is administratively disabled
 * 3) when repriortizing jobs in limited mode
 *
 * The finalize flag is for the first case.  It allows the job to continue
 * through CLEANUP without waiting for the scheduler to respond to the cancel.
 * The sched.alloc response handler must handle the case where the job is
 * no longer active or its alloc_pending flag is clear.  Essentially 'finalize'
 * causes the job related finalization stuff to happen here rather than
 * in the sched.alloc response handler.
 */
int alloc_cancel_alloc_request (struct alloc *alloc,
                                struct job *job,
                                bool finalize)
{
    if (job->alloc_pending) {
        if (cancel_request (alloc, job) < 0)
            return -1;
        if (finalize) {
            job->alloc_pending = 0;
            job_priority_queue_delete (alloc->sent, job);
            annotations_clear_and_publish (alloc->ctx, job, NULL);
        }
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

/* called from reprioritize_job() */
void alloc_queue_reorder (struct alloc *alloc, struct job *job)
{
    job_priority_queue_reorder (alloc->queue, job);
}

void alloc_pending_reorder (struct alloc *alloc, struct job *job)
{
    job_priority_queue_reorder (alloc->sent, job);
}

int alloc_queue_reprioritize (struct alloc *alloc)
{
    job_priority_queue_sort (alloc->queue);
    job_priority_queue_sort (alloc->sent);

    if (alloc->alloc_limit)
        return alloc_queue_recalc_pending (alloc);
    return 0;
}

/* called if highest priority job may have changed */
int alloc_queue_recalc_pending (struct alloc *alloc)
{
    struct job *head = zlistx_first (alloc->queue);
    struct job *tail = zlistx_last (alloc->sent);
    while (alloc->alloc_limit
           && head
           && tail) {
        if (job_priority_comparator (head, tail) < 0) {
            if (alloc_cancel_alloc_request (alloc, tail, false) < 0) {
                flux_log_error (alloc->ctx->h,
                                "%s: alloc_cancel_alloc_request",
                                __FUNCTION__);
                return -1;
            }
        }
        else
            break;
        head = zlistx_next (alloc->queue);
        tail = zlistx_prev (alloc->sent);
    }
    return 0;
}

int alloc_queue_count (struct alloc *alloc)
{
    return zlistx_size (alloc->queue);
}

int alloc_pending_count (struct alloc *alloc)
{
    return zlistx_size (alloc->sent);
}

bool alloc_sched_ready (struct alloc *alloc)
{
    return alloc->scheduler_is_online;
}

static void alloc_query_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;

    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i s:i}",
                           "queue_length", zlistx_size (alloc->queue),
                           "alloc_pending", zlistx_size (alloc->sent),
                           "running", alloc->ctx->running_jobs) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
}

static void resource_status_cb (flux_t *h,
                                flux_msg_handler_t *mh,
                                const flux_msg_t *msg,
                                void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    struct rlist *rl;
    json_t *R = NULL;
    flux_error_t error;
    struct job *job;

    if (!(rl = rlist_create ())) {
        errprintf (&error, "error creating rlist object");
        goto error;
    }
    job = zhashx_first (alloc->ctx->active_jobs);
    while (job) {
        if ((job->has_resources && !job->free_posted)
            && job->R_redacted && !job->alloc_bypass) {
            struct rlist *rl2;
            json_error_t jerror;

            if (!(rl2 = rlist_from_json (job->R_redacted, &jerror))) {
                errprintf (&error,
                           "%s: error converting JSON to rlist: %s",
                           idf58 (job->id),
                           jerror.text);
                goto error;
            }
            if (rlist_append (rl, rl2) < 0) {
                errprintf (&error, "%s: duplicate allocation", idf58 (job->id));
                rlist_destroy (rl2);
                goto error;
            }
            rlist_destroy (rl2);
        }
        job = zhashx_next (alloc->ctx->active_jobs);
    }
    if (housekeeping_stat_append (ctx->housekeeping, rl, &error) < 0)
        goto error;
    if (!(R = rlist_to_R (rl))) {
        errprintf (&error, "error converting rlist to JSON");
        goto error;
    }
    if (flux_respond_pack (h, msg, "{s:O}", "allocated", R) < 0)
        flux_log_error (h, "error responding to resource-status request");
    json_decref (R);
    rlist_destroy (rl);
    return;
error:
    if (flux_respond_error (h, msg, EINVAL, error.text) < 0)
        flux_log_error (h, "error responding to resource-status request");
    json_decref (R);
    rlist_destroy (rl);
}

void alloc_disconnect_rpc (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;

    if (alloc->sched_sender) {
        const char *sender;
        if ((sender = flux_msg_route_first (msg))
            && streq (sender, alloc->sched_sender))
            interface_teardown (ctx->alloc, "disconnect", 0);
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
        zlistx_destroy (&alloc->sent);
        free (alloc->sched_sender);
        free (alloc);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.sched-hello",
        hello_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.sched-ready",
        ready_cb,
        0
    },
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.alloc-query",
        alloc_query_cb,
        FLUX_ROLE_USER,
    },
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.resource-status",
        resource_status_cb,
        0
    },
    {   FLUX_MSGTYPE_RESPONSE,
        "sched.alloc",
        alloc_response_cb,
        0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct alloc *alloc_ctx_create (struct job_manager *ctx)
{
    struct alloc *alloc;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (!(alloc = calloc (1, sizeof (*alloc))))
        return NULL;
    alloc->ctx = ctx;
    if (!(alloc->queue = job_priority_queue_create ())
        || !(alloc->sent = job_priority_queue_create ()))
        goto error;
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
