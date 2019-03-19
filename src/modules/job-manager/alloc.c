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
 * STARTUP:
 *
 * 1) Scheduler sends job-manager.sched-hello request:
 *   <empty payload>
 * Job manager responds with array of jobids that have allocated resources:
 *   {"alloc":[I,I,I,...]}
 * Scheduler should read those jobs' R from KVS and mark resources allocated.
 *
 * 2) Scheduler sends job-manager.sched-ready request:
 *   {"mode":s}
 * mode is scheduler's preference for throttling alloc requests:
 *   "single"    - limit of one pending alloc request (e.g. for FCFS)
 *   "unlimited" - no limit on number of pending alloc requests
 * Job manager responds with alloc queue depth (sched may ignore this)
 *   {"count":i}
 *
 * ALLOCATION (see notes 3-4 below):
 *
 * Job manager sends sched.alloc request:
 *   {"id":I, "priority":i, "userid":i, "t_submit":f}
 * Scheduler responds with:
*    {"id":I, "type":i, "note"?:s}
 * Where type is one of:
 * 0 - resources allocated (sched commits R to KVS before responding)
 * 1 - annotation (just updates note, see below)
 * 2 - job cannot run (note is set to error string)
 * Type 0 or 2 are taken as final responses, type 1 is not.
 *
 * DE-ALLOCATION (see notes 3-4 below):
 *
 * Job manager sends sched.free request:
 *   {"id":I}
 * Scheduler reads R from KVS and marks resources free and responds with
 *   {"id":I}
 *
 * EXCEPTION:
 *
 * Job manager sends a job-exception event:
 *   {"id":I, "type":s, "severity":i}
 * If severity=0 exception is received for a job with a pending alloc request,
 * scheduler responds with with type=2 (error).
 *
 * TEARDOWN:
 *
 * Scheduler sends each internally queued alloc request a regular ENOSYS
 * RPC error response.
 *
 * The receipt of a regular RPC error of any type from the scheduler
 * causes the job manager to assume the scheduler is unloading and to
 * stop sending requests.
 *
 * Notes:
 * 1) scheduler can be loaded after jobs have begun to be submitted
 * 2) scheduler can be unloaded without affecting workload
 * 3) alloc/free requests and responses are matched using jobid, not
 *    the normal matchtag, for scalability.
 * 4) a normal RPC error response to alloc/free triggers teardown
 * 5) 'note' in alloc response is intended to be human readable note displayed
 *    with job info (could be estimated start time, or error detail)
 *
 * TODO:
 * - handle type=1 annotation for queue listing (currently ignored)
 * - implement flow control (credit based?) interface mode
 * - handle post alloc request job priority change
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "queue.h"
#include "job.h"
#include "alloc.h"
#include "queue.h"
#include "event.h"

typedef enum {
    SCHED_SINGLE,       // only allow one outstanding sched.alloc request
    SCHED_UNLIMITED,    // send all sched.alloc requests immediately
} sched_interface_t;

struct alloc_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct queue *queue;    // main active job queue
    struct event_ctx *event_ctx;
    struct queue *inqueue;  // secondary queue of jobs to be scheduled
    sched_interface_t mode;
    bool ready;
    flux_watcher_t *prep;
    flux_watcher_t *check;
    flux_watcher_t *idle;
    unsigned int active_alloc_count; // for mode=single, max of 1
};

/* Initiate teardown.  Clear any alloc/free requests, and clear
 * the alloc->ready flag to stop prep/check from allocating.
 */
static void interface_teardown (struct alloc_ctx *ctx, char *s, int errnum)
{
    if (ctx->ready) {
        struct job *job;

        flux_log (ctx->h, LOG_DEBUG, "alloc: stop due to %s: %s",
                  s, flux_strerror (errnum));

        job = queue_first (ctx->queue);
        while (job) {
            /* jobs with alloc pending need to go back in the queue
             * so they will automatically send alloc again.
             */
            if (job->alloc_pending) {
                assert (job->aux_queue_handle == NULL);
                if (queue_insert (ctx->inqueue, job,
                                                &job->aux_queue_handle) < 0)
                    flux_log_error (ctx->h, "%s: queue_insert", __FUNCTION__);
                job->alloc_pending = 0;
                job->alloc_queued = 1;
            }
            /* jobs with free pending (much smaller window for this to be true)
             * need to be picked up again after 'hello'.
             */
            job->free_pending = 0;
            job = queue_next (ctx->queue);
        }
        ctx->ready = false;
        ctx->active_alloc_count = 0;
    }
}

/* Handle a sched.free response.
 */
static void free_response_cb (flux_t *h, flux_msg_handler_t *mh,
                              const flux_msg_t *msg, void *arg)
{
    struct alloc_ctx *ctx = arg;
    flux_jobid_t id = 0;
    struct job *job;

    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto teardown;
    if (flux_msg_unpack (msg, "{s:I}", "id", &id) < 0)
        goto teardown;
    if (!(job = queue_lookup_by_id (ctx->queue, id))) {
        flux_log_error (h, "sched.free-response: id=%llu not active",
                        (unsigned long long)id);
        goto teardown;
    }
    if (!job->has_resources) {
        flux_log (h, LOG_ERR, "sched.free-response: id=%lld not allocated",
                  (unsigned long long)id);
        errno = EINVAL;
        goto teardown;
    }
    job->free_pending = 0;
    if (event_job_post (ctx->event_ctx, job, NULL, NULL, "free", NULL) < 0)
        goto teardown;
    return;
teardown:
    interface_teardown (ctx, "free response error", errno);
}

/* Send sched.free request for job.
 * Update flags.
 */
int free_request (struct alloc_ctx *ctx, struct job *job)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("sched.free", NULL)))
        return -1;
    if (flux_msg_pack (msg, "{s:I}", "id", job->id) < 0)
        goto error;
    if (flux_send (ctx->h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

/* Handle a sched.alloc response.
 * Update flags.
 */
static void alloc_response_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct alloc_ctx *ctx = arg;
    flux_jobid_t id = 0;
    int type = -1;
    const char *note = NULL;
    struct job *job;

    if (flux_response_decode (msg, NULL, NULL) < 0)
        goto teardown; // ENOSYS here if scheduler not loaded/shutting down
    if (flux_msg_unpack (msg, "{s:I s:i s?:s}",
                              "id", &id,
                              "type", &type,
                              "note", &note) < 0)
        goto teardown;
    if (type != 0 && type != 1 && type != 2) {
        errno = EPROTO;
        goto teardown;
    }
    if (!(job = queue_lookup_by_id (ctx->queue, id))) {
        flux_log_error (h, "sched.alloc-response: id=%llu not active",
                        (unsigned long long)id);
        goto teardown;
    }
    if (!job->alloc_pending) {
        flux_log (h, LOG_ERR, "sched.alloc-response: id=%lld not requested",
                  (unsigned long long)id);
        errno = EINVAL;
        goto teardown;
    }

    /* Handle job annotation update.
     * This does not terminate the response stream.
     */
    if (type == 1) {
        // FIXME
        return;
    }

    ctx->active_alloc_count--;
    job->alloc_pending = 0;

    /* Handle alloc error.
     * Raise alloc exception and transition to CLEANUP state.
     */
    if (type == 2) { // error: alloc was rejected
        if (event_job_post_pack (ctx->event_ctx, job, NULL, NULL, "exception",
                                 "{ s:s s:i s:i s:s }",
                                 "type", "alloc",
                                 "severity", 0,
                                 "userid", FLUX_USERID_UNKNOWN,
                                 "note", note ? note : "") < 0)
            goto teardown;
        return;
    }

    /* Handle alloc success (type == 0)
     * Log alloc event and transtion to RUN state.
     */
    if (job->has_resources) {
        flux_log (h, LOG_ERR, "sched.alloc-response: id=%lld already allocated",
                  (unsigned long long)id);
        errno = EEXIST;
        goto teardown;
    }

    job->has_resources = 1;

    if (event_job_post_pack (ctx->event_ctx, job, NULL, NULL, "alloc",
                             "{ s:s }",
                             "note", note ? note : "") < 0)
        goto teardown;
    return;
teardown:
    interface_teardown (ctx, "alloc response error", errno);
}

/* Send sched.alloc request for job.
 * Update flags.
 */
int alloc_request (struct alloc_ctx *ctx, struct job *job)
{
    flux_msg_t *msg;

    if (!(msg = flux_request_encode ("sched.alloc", NULL)))
        return -1;
    if (flux_msg_pack (msg, "{s:I s:i s:i s:f}",
                            "id", job->id,
                            "priority", job->priority,
                            "userid", job->userid,
                            "t_submit", job->t_submit) < 0)
        goto error;
    if (flux_send (ctx->h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    return 0;
error:
    flux_msg_destroy (msg);
    return -1;
}

/* sched-hello:
 * Scheduler obtains a list of jobs that have resources allocated.
 */
static void hello_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct alloc_ctx *ctx = arg;
    struct job *job;
    json_t *o = NULL;
    json_t *jobid;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "scheduler: hello");
    if (!(o = json_array ()))
        goto nomem;
    job = queue_first (ctx->queue);
    while (job) {
        if (job->has_resources) {
            if (!(jobid = json_integer (job->id)))
                goto nomem;
            if (json_array_append_new (o, jobid) < 0) {
                json_decref (jobid);
                goto nomem;
            }
        }
        job = queue_next (ctx->queue);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "alloc", o) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    /* Restart any free requests that might have been interrupted
     * when scheduler was last unloaded.
     */
    job = queue_first (ctx->queue);
    while (job) {
        if (event_job_action (ctx->event_ctx, job) < 0)
            flux_log_error (h, "%s: event_job_action", __FUNCTION__);
        job = queue_next (ctx->queue);
    }
    json_decref (o);
    return;
nomem:
    errno = ENOMEM;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (o);
}

/* sched-ready:
 * Scheduler indicates what style of alloc concurrency is requires,
 * and tells job-manager to start allocations.  job-manager tells
 * scheduler how many jobs are in the queue.
 */
static void ready_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct alloc_ctx *ctx = arg;
    const char *mode;
    int count;

    if (flux_request_unpack (msg, NULL, "{s:s}", "mode", &mode) < 0)
        goto error;
    if (!strcmp (mode, "single"))
        ctx->mode = SCHED_SINGLE;
    else if (!strcmp (mode, "unlimited"))
        ctx->mode = SCHED_UNLIMITED;
    else {
        errno = EPROTO;
        goto error;
    }
    ctx->ready = true;
    flux_log (h, LOG_DEBUG, "scheduler: ready %s", mode);
    count = queue_size (ctx->inqueue);
    if (flux_respond_pack (h, msg, "{s:i}", "count", count) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
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
    struct alloc_ctx *ctx = arg;
    if (!ctx->ready)
        return;
    if (ctx->mode == SCHED_SINGLE && ctx->active_alloc_count > 0)
        return;
    if (queue_first (ctx->inqueue))
        flux_watcher_start (ctx->idle);
}

/* check:
 * Runs right after reactor calls poll(2).
 * Stop idle watcher, and send next alloc request, if available.
 */
static void check_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    struct alloc_ctx *ctx = arg;
    struct job *job;

    flux_watcher_stop (ctx->idle);
    if (!ctx->ready)
        return;
    if (ctx->mode == SCHED_SINGLE && ctx->active_alloc_count > 0)
        return;
    if ((job = queue_first (ctx->inqueue))) {
        if (alloc_request (ctx, job) < 0) {
            flux_log_error (ctx->h, "alloc_request fatal error");
            flux_reactor_stop_error (flux_get_reactor (ctx->h));
            return;
        }
        queue_delete (ctx->inqueue, job, job->aux_queue_handle);
        job->aux_queue_handle = NULL;
        job->alloc_pending = 1;
        job->alloc_queued = 0;
        ctx->active_alloc_count++;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post (ctx->event_ctx, job, NULL, NULL,
                                 "debug.alloc-request", NULL);

    }
}

int alloc_send_free_request (struct alloc_ctx *ctx, struct job *job)
{
    assert (job->state == FLUX_JOB_CLEANUP);
    if (!job->free_pending) {
        if (free_request (ctx, job) < 0)
            return -1;
        job->free_pending = 1;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post (ctx->event_ctx, job, NULL, NULL,
                                  "debug.free-request", NULL);
    }
    return 0;
}

int alloc_enqueue_alloc_request (struct alloc_ctx *ctx, struct job *job)
{
    assert (job->state == FLUX_JOB_SCHED);
    if (!job->alloc_queued && !job->alloc_pending) {
        assert (job->aux_queue_handle == NULL);
        if (queue_insert (ctx->inqueue, job, &job->aux_queue_handle) < 0)
            return -1;
        job->alloc_queued = 1;
    }
    return 0;
}

void alloc_ctx_destroy (struct alloc_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;;
        flux_msg_handler_delvec (ctx->handlers);
        flux_watcher_destroy (ctx->prep);
        flux_watcher_destroy (ctx->check);
        flux_watcher_destroy (ctx->idle);
        queue_destroy (ctx->inqueue);
        free (ctx);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "job-manager.sched-hello", hello_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "job-manager.sched-ready", ready_cb, 0},
    { FLUX_MSGTYPE_RESPONSE, "sched.alloc", alloc_response_cb, 0},
    { FLUX_MSGTYPE_RESPONSE, "sched.free", free_response_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct alloc_ctx *alloc_ctx_create (flux_t *h, struct queue *queue,
                                    struct event_ctx *event_ctx)
{
    struct alloc_ctx *ctx;
    flux_reactor_t *r = flux_get_reactor (h);

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    ctx->queue = queue;
    ctx->event_ctx = event_ctx;
    if (!(ctx->inqueue = queue_create (false)))
        goto error;
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    ctx->prep = flux_prepare_watcher_create (r, prep_cb, ctx);
    ctx->check = flux_check_watcher_create (r, check_cb, ctx);
    ctx->idle = flux_idle_watcher_create (r, NULL, NULL);
    if (!ctx->prep || !ctx->check || !ctx->idle) {
        errno = ENOMEM;
        goto error;
    }
    flux_watcher_start (ctx->prep);
    flux_watcher_start (ctx->check);
    event_ctx_set_alloc_ctx (event_ctx, ctx);
    return ctx;
error:
    alloc_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
