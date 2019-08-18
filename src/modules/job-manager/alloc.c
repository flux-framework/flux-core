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
 * Job manager responds with array of job objects that have allocated resources:
 *   {"alloc":[{"id":I, "priority":i, "userid":i, "t_submit":f},{},{},...]}
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
#include <czmq.h>
#include <flux/core.h>
#include <assert.h>

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "simulator.h"

typedef enum {
    SCHED_SINGLE,       // only allow one outstanding sched.alloc request
    SCHED_UNLIMITED,    // send all sched.alloc requests immediately
} sched_interface_t;

struct alloc {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlistx_t *pending;  // queue of jobs to be scheduled
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
static void interface_teardown (struct alloc *alloc, char *s, int errnum)
{
    if (alloc->ready) {
        struct job *job;
        struct job_manager *ctx = alloc->ctx;

        flux_log (ctx->h, LOG_DEBUG, "alloc: stop due to %s: %s",
                  s, flux_strerror (errnum));

        job = zhashx_first (ctx->active_jobs);
        while (job) {
            /* jobs with alloc pending need to go back in the queue
             * so they will automatically send alloc again.
             */
            if (job->alloc_pending) {
                bool fwd = job->priority > FLUX_JOB_PRIORITY_DEFAULT ? true
                                                                     : false;

                assert (job->handle == NULL);
                if (!(job->handle = zlistx_insert (alloc->pending, job, fwd)))
                    flux_log_error (ctx->h, "%s: queue_insert", __FUNCTION__);
                job->alloc_pending = 0;
                job->alloc_queued = 1;
            }
            /* jobs with free pending (much smaller window for this to be true)
             * need to be picked up again after 'hello'.
             */
            job->free_pending = 0;
            job = zhashx_next (ctx->active_jobs);
        }
        alloc->ready = false;
        alloc->active_alloc_count = 0;
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
    if (event_job_post_pack (ctx->event, job, "free", NULL) < 0)
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

/* Handle a sched.alloc response.
 * Update flags.
 */
static void alloc_response_cb (flux_t *h, flux_msg_handler_t *mh,
                               const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;
    flux_jobid_t id = 0;
    int type = -1;
    const char *note = NULL;
    struct job *job;

    sim_received_alloc_response (ctx->simulator);

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

    /* Handle job annotation update.
     * This does not terminate the response stream.
     */
    if (type == 1) {
        // FIXME
        return;
    }

    alloc->active_alloc_count--;
    job->alloc_pending = 0;

    /* Handle alloc error.
     * Raise alloc exception and transition to CLEANUP state.
     */
    if (type == 2) { // error: alloc was rejected
        if (event_job_post_pack (ctx->event, job, "exception",
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
        flux_log (h, LOG_ERR, "sched.alloc-response: id=%ju already allocated",
                  (uintmax_t)id);
        errno = EEXIST;
        goto teardown;
    }

    if (event_job_post_pack (ctx->event, job, "alloc",
                             "{ s:s }",
                             "note", note ? note : "") < 0)
        goto teardown;
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
    if (flux_msg_pack (msg, "{s:I s:i s:i s:f}",
                            "id", job->id,
                            "priority", job->priority,
                            "userid", job->userid,
                            "t_submit", job->t_submit) < 0)
        goto error;
    if (flux_send (alloc->ctx->h, msg, 0) < 0)
        goto error;
    flux_msg_destroy (msg);
    sim_sending_sched_request (alloc->ctx->simulator);
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
    struct job_manager *ctx = arg;
    struct job *job;
    json_t *o = NULL;
    json_t *entry;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "scheduler: hello");
    if (!(o = json_array ()))
        goto nomem;
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (job->has_resources) {
            if (!(entry = json_pack ("{s:I s:i s:i s:f}",
                                     "id", job->id,
                                     "priority", job->priority,
                                     "userid", job->userid,
                                     "t_submit", job->t_submit)))
                goto nomem;
            if (json_array_append_new (o, entry) < 0) {
                json_decref (entry);
                goto nomem;
            }
        }
        job = zhashx_next (ctx->active_jobs);
    }
    if (flux_respond_pack (h, msg, "{s:O}", "alloc", o) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    /* Restart any free requests that might have been interrupted
     * when scheduler was last unloaded.
     */
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        /* N.B. first/next are NOT deletion safe but event_job_action()
         * won't call zhashx_delete() for jobs in FLUX_JOB_CLEANUP state.
         */
        if (job->state == FLUX_JOB_CLEANUP && job->has_resources) {
            if (event_job_action (ctx->event, job) < 0)
                flux_log_error (h, "%s: event_job_action", __FUNCTION__);
        }
        job = zhashx_next (ctx->active_jobs);
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
    struct job_manager *ctx = arg;
    const char *mode;
    int count;

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
    ctx->alloc->ready = true;
    flux_log (h, LOG_DEBUG, "scheduler: ready %s", mode);
    count = zlistx_size (ctx->alloc->pending);
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
    struct job_manager *ctx = arg;
    struct alloc *alloc = ctx->alloc;

    if (!alloc->ready)
        return;
    if (alloc->mode == SCHED_SINGLE && alloc->active_alloc_count > 0)
        return;
    if (zlistx_first (alloc->pending))
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
    if (!alloc->ready)
        return;
    if (alloc->mode == SCHED_SINGLE && alloc->active_alloc_count > 0)
        return;
    if ((job = zlistx_first (alloc->pending))) {
        if (alloc_request (alloc, job) < 0) {
            flux_log_error (ctx->h, "alloc_request fatal error");
            flux_reactor_stop_error (flux_get_reactor (ctx->h));
            return;
        }
        zlistx_delete (alloc->pending, job->handle);
        job->handle = NULL;
        job->alloc_pending = 1;
        job->alloc_queued = 0;
        alloc->active_alloc_count++;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post_pack (ctx->event, job,
                                       "debug.alloc-request", NULL);

    }
}

int alloc_send_free_request (struct alloc *alloc, struct job *job)
{
    assert (job->state == FLUX_JOB_CLEANUP);
    if (!job->free_pending) {
        if (free_request (alloc, job) < 0)
            return -1;
        job->free_pending = 1;
        if ((job->flags & FLUX_JOB_DEBUG))
            (void)event_job_post_pack (alloc->ctx->event, job,
                                       "debug.free-request", NULL);
    }
    return 0;
}

int alloc_enqueue_alloc_request (struct alloc *alloc, struct job *job)
{
    assert (job->state == FLUX_JOB_SCHED);
    if (!job->alloc_queued && !job->alloc_pending) {
    bool fwd = job->priority > FLUX_JOB_PRIORITY_DEFAULT ? true : false;
        assert (job->handle == NULL);
        if (!(job->handle = zlistx_insert (alloc->pending, job, fwd)))
            return -1;
        job->alloc_queued = 1;
    }
    return 0;
}

void alloc_dequeue_alloc_request (struct alloc *alloc, struct job *job)
{
    if (job->alloc_queued) {
        zlistx_delete (alloc->pending, job->handle);
        job->handle = NULL;
        job->alloc_queued = 0;
        alloc->active_alloc_count--;
    }
}

struct job *alloc_pending_first (struct alloc *alloc)
{
    return zlistx_first (alloc->pending);
}

struct job *alloc_pending_next (struct alloc *alloc)
{
    return zlistx_next (alloc->pending);
}

void alloc_pending_reorder (struct alloc *alloc, struct job *job)
{
    bool fwd = job->priority > FLUX_JOB_PRIORITY_DEFAULT ? true : false;

    zlistx_reorder (alloc->pending, job->handle, fwd);
}

void alloc_ctx_destroy (struct alloc *alloc)
{
    if (alloc) {
        int saved_errno = errno;;
        flux_msg_handler_delvec (alloc->handlers);
        flux_watcher_destroy (alloc->prep);
        flux_watcher_destroy (alloc->check);
        flux_watcher_destroy (alloc->idle);
        zlistx_destroy (&alloc->pending);
        free (alloc);
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

struct alloc *alloc_ctx_create (struct job_manager *ctx)
{
    struct alloc *alloc;
    flux_reactor_t *r = flux_get_reactor (ctx->h);

    if (!(alloc = calloc (1, sizeof (*alloc))))
        return NULL;
    alloc->ctx = ctx;
    if (!(alloc->pending = zlistx_new()))
        goto error;
    zlistx_set_destructor (alloc->pending, job_destructor);
    zlistx_set_comparator (alloc->pending, job_pending_cmp);
    zlistx_set_duplicator (alloc->pending, job_duplicator);

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
