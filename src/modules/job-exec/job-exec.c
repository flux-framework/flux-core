/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Prototype flux job exec service
 *
 * DESCRIPTION
 *
 * This module implements the exec interface as described in
 * job-manager/start.c, but does not currently support execution of
 * real work. Execution is simulated by setting a timer for the duration
 * specified in either the jobspec system.duration attribute or a test
 * duration in system.exec.test.run_duration.  The module can optionally
 * simulate mock exceptions during run or initialization.
 * See TEST CONFIGURATION below.
 *
 * OPERATION
 *
 * For details of startup protocol, see job-manager/start.c.
 *
 * JOB INIT:
 *
 * On receipt of a start request, the exec service enters initialization
 * phase of the job, where the jobspec and R are fetched from the KVS,
 * and the guest namespace is created and linked from the primary
 * namespace. A guest.exec.eventlog is created with an initial "init"
 * event posted.
 *
 * Jobspec and R are parsed as soon as asynchronous initialization tasks
 * complete. If any of these steps fail, an exec initialization exception
 * is thrown. Finally, the implementation "init" method is called.
 *
 * JOB STARTING/RUNNING:
 *
 * After initialization is complete, the exec service emits a "starting"
 * event to the exec eventlog and calls the implementation "start" method.
 * Once all job shells or equivalent are running, the exec implementation
 * should invoke jobinfo_started(), which emits a "running" event to the
 * exec eventlog and sends the "start" response to the job-manager.
 *
 * JOB FINISH/CLEANUP:
 *
 * As tasks/job shells exit, the exec implementation should call
 * jobinfo_tasks_complete(), which emits a "complete" event to the exec
 * eventlog, sends a "finish" response to the job-manager.
 *
 * JOB FINALIZATION:
 *
 * jobinfo_finalize() is called after the "finish" event, which performs
 * the following tasks:
 *
 *  - terminating "done" event is posted to the exec.eventlog
 *  - the guest namespace, now quiesced, is copied to the primary namespace
 *  - the guest namespace is removed
 *  - the final "release final=true" response is sent to the job manager
 *  - the local job object is destroyed
 *
 * TEST CONFIGURATION
 *
 * The job-exec module supports an object in the jobspec under
 * attributes.system.exec.test, which enables mock execution and
 * supports the following keys
 *
 * {
 *   "run_duration":s,      - alternate/override attributes.system.duration
 *   "wait_status":i        - report this value as status in the "finish" resp
 *   "mock_exception":s     - mock an exception during this phase of job
 *                             execution (currently "init" and "run")
 * }
 *
 * The "bulk" execution implementation supports testing and other
 * parameters under attributes.system.exec.bulkexec, including:
 *
 * {
 *   "mock_exception":s     - cancel job after a certain number of shells
 *                            have been launched.
 * }
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libeventlog/eventlogger.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/sigutil.h"
#include "ccan/str/str.h"

#include "job-exec.h"
#include "checkpoint.h"
#include "exec_config.h"

static double kill_timeout;
static int max_kill_count;
static int term_signal;
static int kill_signal;

#define DEBUG_FAIL_EXPIRATION 1

extern struct exec_implementation testexec;
extern struct exec_implementation bulkexec;

static struct exec_implementation * implementations[] = {
    &testexec,
    &bulkexec,
    NULL
};

struct job_exec_ctx {
    flux_t *              h;
    int                   argc; /* needed for later reparse */
    char **               argv; /* needed for later reparse */
    flux_msg_handler_t ** handlers;
    zhashx_t *            jobs;
};

void jobinfo_incref (struct jobinfo *job)
{
    job->refcount++;
}

void jobinfo_decref (struct jobinfo *job)
{
    if (job && (--job->refcount == 0)) {
        int saved_errno = errno;
        idset_destroy (job->critical_ranks);
        eventlogger_destroy (job->ev);
        flux_watcher_destroy (job->kill_timer);
        flux_watcher_destroy (job->kill_shell_timer);
        flux_watcher_destroy (job->expiration_timer);
        zhashx_delete (job->ctx->jobs, &job->id);
        if (job->impl && job->impl->exit)
            (*job->impl->exit) (job);
        job->ctx = NULL;
        flux_msg_decref (job->req);
        job->req = NULL;
        resource_set_destroy (job->R);
        json_decref (job->jobspec);
        free (job->rootref);
        free (job);
        errno = saved_errno;
    }
}

static struct jobinfo * jobinfo_new (void)
{
    struct jobinfo *job = calloc (1, sizeof (*job));
    job->refcount = 1;
    return job;
}

flux_future_t *jobinfo_shell_rpc_pack (struct jobinfo *job,
                                       const char *topic,
                                       const char *fmt,
                                       ...)
{
    va_list ap;
    char *shell_topic = NULL;
    flux_future_t *f = NULL;
    uint32_t rank;
    char idbuf[21];

    if (flux_job_id_encode (job->id, "f58plain", idbuf, sizeof (idbuf)) < 0
        || asprintf (&shell_topic,
                     "%ju-shell-%s.%s",
                     (uintmax_t) job->userid,
                     idbuf,
                     topic) < 0
        || ((rank = resource_set_nth_rank (job->R, 0)) == IDSET_INVALID_ID))
        goto out;
    va_start (ap, fmt);
    f = flux_rpc_vpack (job->ctx->h, shell_topic, rank, 0, fmt, ap);
    va_end (ap);
    flux_future_aux_set (f, "jobinfo", job, (flux_free_f) jobinfo_decref);
    jobinfo_incref (job);
out:
    ERRNO_SAFE_WRAP (free, shell_topic);
    return f;
}

/*  Emit an event to the exec system eventlog and return a future from
 *   flux_kvs_commit().
 */
static flux_future_t * jobinfo_emit_event_vpack (struct jobinfo *job,
                                                 const char *name,
                                                 const char *fmt,
                                                 va_list ap)
{
    int saved_errno;
    flux_t *h = job->ctx->h;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    json_t *entry = NULL;
    char *entrystr = NULL;
    const char *key = "exec.eventlog";

    if (!(entry = eventlog_entry_vpack (0., name, fmt, ap))) {
        flux_log_error (h, "emit event: eventlog_entry_vpack");
        return NULL;
    }
    if (!(entrystr = eventlog_entry_encode (entry))) {
        flux_log_error (h, "emit event: eventlog_entry_encode");
        goto out;
    }
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "emit event: flux_kvs_txn_create");
        goto out;
    }
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, entrystr) < 0) {
        flux_log_error (h, "emit event: flux_kvs_txn_put");
        goto out;
    }
    if (!(f = flux_kvs_commit (h, job->ns, 0, txn)))
        flux_log_error (h, "emit event: flux_kvs_commit");
out:
    saved_errno = errno;
    json_decref (entry);
    free (entrystr);
    flux_kvs_txn_destroy (txn);
    errno = saved_errno;
    return f;
}

static flux_future_t * jobinfo_emit_event_pack (struct jobinfo *job,
                                                const char *name,
                                                const char *fmt, ...)
{
    flux_future_t *f = NULL;
    va_list ap;
    va_start (ap, fmt);
    f = jobinfo_emit_event_vpack (job, name, fmt, ap);
    va_end (ap);
    return f;
}

static int jobinfo_emit_event_vpack_nowait (struct jobinfo *job,
                                            const char *name,
                                            const char *fmt,
                                            va_list ap)
{
    return eventlogger_append_vpack (job->ev,
                                     0,
                                     "exec.eventlog",
                                     name, fmt, ap);
}

/*
 *  Send an event "open loop" -- takes a reference to the job and
 *   releases it in the continuation, logging an error if one was
 *   received.
 */
int jobinfo_emit_event_pack_nowait (struct jobinfo *job,
                                    const char *name,
                                    const char *fmt, ...)
{
    int rc = -1;
    va_list ap;
    va_start (ap, fmt);
    rc = jobinfo_emit_event_vpack_nowait (job, name, fmt, ap);
    va_end (ap);
    return rc;
}

static int jobid_exception (flux_t *h,
                            flux_jobid_t id,
                            const flux_msg_t *msg,
                            const char *type,
                            int severity,
                            int errnum,
                            const char *text)
{
    char note [256];
    if (errnum)
        snprintf (note, sizeof (note), "%s%s%s",
                                        text ? text : "",
                                        text ? ": " : "",
                                        strerror (errnum));
    else
        snprintf (note, sizeof (note), "%s", text ? text : "");

    flux_log (h,
              LOG_INFO,
              "job-exception: id=%s: %s",
              idf58 (id),
              note);
    return flux_respond_pack (h, msg, "{s:I s:s s:{s:i s:s s:s}}",
                                      "id", id,
                                      "type", "exception",
                                      "data",
                                      "severity", severity,
                                      "type", type,
                                      "note", note);
}

static int jobinfo_respond_error (struct jobinfo *job,
                                  int errnum,
                                  const char *msg)
{
    return jobid_exception (job->ctx->h,
                            job->id,
                            job->req,
                            "exec",
                            0,
                            errnum,
                            msg);
}

static int jobinfo_send_release (struct jobinfo *job,
                                 const struct idset *idset)
{
    int rc;
    flux_t *h = job->ctx->h;
    // XXX: idset ignored for now. Always release all resources
    rc = flux_respond_pack (h,
                            job->req,
                            "{s:I s:s s{s:s s:b}}",
                            "id", job->id,
                            "type", "release",
                            "data",
                              "ranks", "all",
                              "final", true);
    return rc;
}

static int jobinfo_respond (flux_t *h,
                            struct jobinfo *job,
                            const char *event)
{
    return flux_respond_pack (h,
                              job->req,
                              "{s:I s:s s:{}}",
                              "id", job->id,
                              "type", event,
                              "data");
}

static void jobinfo_complete (struct jobinfo *job, const struct idset *ranks)
{
    flux_t *h = job->ctx->h;
    job->running = 0;

    if (job->exception_in_progress && job->wait_status == 0)
        job->wait_status = 1<<8;

    if (h && job->req) {
        jobinfo_emit_event_pack_nowait (job, "complete",
                                        "{ s:i }",
                                        "status", job->wait_status);
        if (flux_respond_pack (h,
                               job->req,
                               "{s:I s:s s:{s:i}}",
                               "id", job->id,
                               "type", "finish",
                               "data",
                                 "status", job->wait_status) < 0)
            flux_log_error (h, "jobinfo_complete: flux_respond");
    }
}

int jobinfo_drain_ranks (struct jobinfo *job,
                         const char *ranks,
                         const char *fmt,
                         ...)
{
    va_list ap;
    flux_future_t *f = NULL;
    char reason[256];
    int rc = -1;

    /* vsnprintf(3) May truncate (unlikely), but that's ok 255 chars should
     * get the gist across...
     */
    va_start (ap, fmt);
    (void) vsnprintf (reason, sizeof (reason), fmt, ap);
    va_end (ap);

    if (!(f = flux_rpc_pack (job->h,
                             "resource.drain",
                             0,
                             0,
                             "{s:s s:s s:s}",
                             "targets", ranks,
                             "reason", reason,
                             "mode", "update"))) {
        flux_log (job->h,
                  LOG_ERR,
                  "Failed to drain broker ranks %s for job %s",
                  ranks,
                  idf58 (job->id));
        goto error;
    }
    rc = 0;
error:
    flux_future_destroy (f);
    return rc;
}

static int drain_active_ranks (struct jobinfo *job, struct idset *active_ranks)
{
    char *ranks = NULL;
    int rc;

    if (!(ranks = idset_encode (active_ranks, IDSET_FLAG_RANGE))) {
        flux_log_error (job->h,
                        "drain_active_ranks: Failed get rank string for job %s",
                        idf58 (job->id));
        return -1;
    }
    rc = jobinfo_drain_ranks (job,
                              ranks,
                              "unkillable processes for job %s",
                              idf58 (job->id));
    ERRNO_SAFE_WRAP (free, ranks);
    return rc;
}

static void kill_shell_timer_cb (flux_reactor_t  *r,
                                 flux_watcher_t *w,
                                 int revents,
                                 void *arg)
{
    struct jobinfo *job = arg;
    struct idset *active_ranks;
    int actual_kill_signal = kill_signal;

    /* RFC 15 states that the IMP handles SIGUSR1 by sending SIGKILL to
     * the entire cgroup.  Sending SIGKILL to the IMP is not productive.
     */
    if (job->multiuser)
        actual_kill_signal = SIGUSR1;

    flux_log (job->h,
              LOG_DEBUG,
              "Sending %s to %s for job %s",
              sigutil_signame (actual_kill_signal),
              job->multiuser ? "IMP" : "job shell",
              idf58 (job->id));
    (*job->impl->kill) (job, actual_kill_signal);
    job->kill_shell_count++;

    /* Since we've transitioned to killing the shell directly, stop the
     * flux_job_kill(3) timer:
     */
    flux_watcher_stop (job->kill_timer);

    /*  Reuse job->kill_timer to create an exponential backoff
     */
    if ((job->kill_timeout = job->kill_timeout * 2) > 300.)
        job->kill_timeout = 300.;
    flux_timer_watcher_reset (w, job->kill_timeout, 0.);
    flux_watcher_start (w);

    /* Check if we've exceeded the maximum number of kill attempts.
     * Drain ranks that are still active if so. If the drain succeeds,
     * then force remaining tasks to be complete, which will then cause
     * this job object to be destroyed (which terminates any active kill
     * timers).
     *
     * If the drain fails (unlikely), then the job stays active and we'll
     * try to kill it again (and drain ranks) the next time the kill timer
     * fires.
     */
    if (job->kill_shell_count >= max_kill_count
        && job->impl->active_ranks
        && (active_ranks = (*job->impl->active_ranks) (job))) {
        flux_log (job->h,
                  LOG_DEBUG,
                  "job %s exceeded max kill count. Draining active ranks",
                  idf58 (job->id));
        if (drain_active_ranks (job, active_ranks) == 0) {
            /* Force remaining tasks to be complete
             */
            jobinfo_tasks_complete (job, active_ranks, 1);
        }
        idset_destroy (active_ranks);
    }
}

static void kill_timer_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct jobinfo *job = arg;
    flux_future_t *f;
    flux_log (job->h,
              LOG_DEBUG,
              "Sending %s to job %s",
              sigutil_signame (kill_signal),
              idf58 (job->id));
    if (!(f = flux_job_kill (job->h, job->id, kill_signal))) {
        flux_log_error (job->h,
                        "flux_job_kill (%s, %s)",
                        idf58 (job->id),
                        sigutil_signame (kill_signal));
        return;
    }
    job->kill_count++;
    /* Open loop */
    flux_future_destroy (f);
}


static void jobinfo_killtimer_start (struct jobinfo *job, double after)
{
    flux_reactor_t *r = flux_get_reactor (job->h);

    /* Only start kill timer if not already running */
    if (job->kill_timer == NULL) {
        job->kill_timer = flux_timer_watcher_create (r,
                                                     after,
                                                     after,
                                                     kill_timer_cb,
                                                     job);
        flux_watcher_start (job->kill_timer);
    }
    if (job->kill_shell_timer == NULL) {
        job->kill_shell_timer = flux_timer_watcher_create (r,
                                                           after*5,
                                                           0.,
                                                           kill_shell_timer_cb,
                                                           job);
        flux_watcher_start (job->kill_shell_timer);
    }

}

static void timelimit_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct jobinfo *job = arg;

    /*  Timelimit reached. Generate "timeout" exception and send SIGALRM.
     *  Wait for a gracetime then forcibly terminate job.
     */
    if (jobid_exception (job->h,
                         job->id,
                         job->req,
                         "timeout",
                         0,
                         0,
                         "resource allocation expired") < 0)
        flux_log_error (job->h,
                        "failed to generate timeout exception for %s",
                        idf58 (job->id));
    (*job->impl->kill) (job, SIGALRM);
    flux_watcher_stop (w);
    job->exception_in_progress = 1;
    jobinfo_killtimer_start (job, job->kill_timeout);
}

static int jobinfo_set_expiration (struct jobinfo *job)
{
    flux_watcher_t *w = NULL;
    double now;
    double expiration = resource_set_expiration (job->R);
    double starttime = resource_set_starttime (job->R);
    double offset;

    if (expiration < 0.) {
        jobinfo_fatal_error (job,
                             EINVAL,
                             "Invalid resource set expiration %.2f",
                             expiration);
        return -1;
    }

    flux_watcher_destroy (job->expiration_timer);
    job->expiration_timer = NULL;

    /* Timelimit disabled if expiration is set to 0.
     */
    if (expiration == 0.)
        return 0;

    /* N.B. Use of flux_reactor_time(3) here instead of flux_reactor_now(3)
     *  is purposeful, Since this is used to find the time the job has
     *  remaining, we should be as accurate as possible.
     */
    now = flux_reactor_time ();
    if (job->t0 == 0.)
       job->t0 = now;

    /* Adjust expiration time based on delay between when scheduler
     *  created R and when we received this job. O/w jobs may be
     *  terminated due to timeouts prematurely when the system
     *  is very busy, which can cause long delays between alloc and
     *  start events.
     */
    if (starttime > 0.)
        expiration += job->t0 - starttime;

    offset = expiration - now;
    if (offset < 0.) {
        /*  Expiration has already passed. Just set offset to 0. so that
         *  the expiration timer will fire immediately.
         */
        offset = 0.;
    }
    if (!(w = flux_timer_watcher_create (flux_get_reactor(job->h),
                                         offset,
                                         0.,
                                         timelimit_cb,
                                         job))) {
        jobinfo_fatal_error (job, errno, "unable to start expiration timer");
        return -1;
    }
    flux_watcher_start (w);
    job->expiration_timer = w;
    return 0;
}


void jobinfo_started (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    if (h && job->req) {
        if (jobinfo_set_expiration (job) < 0)
            flux_log_error (h,
                            "failed to set expiration for %s",
                            idf58 (job->id));
        job->running = 1;
        if (jobinfo_respond (h, job, "start") < 0)
            flux_log_error (h, "jobinfo_started: flux_respond");
    }
}

void jobinfo_reattached (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    if (h && job->req) {
        job->running = 1;
        if (jobinfo_respond (h, job, "reattached") < 0)
            flux_log_error (h, "jobinfo_reattach: flux_respond");
    }
}

/*  Cancel any pending shells to execute with implementations cancel
 *   method, send term_signal to executing shells to notify them to terminate,
 *   schedule kill_signal to be sent after kill_timeout seconds.
 */
static void jobinfo_cancel (struct jobinfo *job)
{
    /*  If a kill-timer is already active, the cancellation is in progress */
    if (job->kill_timer)
        return;

    if (job->impl->cancel)
        (*job->impl->cancel) (job);

    (*job->impl->kill) (job, term_signal);
    jobinfo_killtimer_start (job, job->kill_timeout);
}

static int jobinfo_finalize (struct jobinfo *job);

static void jobinfo_fatal_verror (struct jobinfo *job,
                                  int errnum,
                                  const char *fmt,
                                  va_list ap)
{
    int n;
    char msg [256];
    int msglen = sizeof (msg);
    flux_t *h = job->ctx->h;

    if ((n = vsnprintf (msg, msglen, fmt, ap)) < 0)
        strcpy (msg, "vsnprintf error");
    else if (n >= msglen) {
        msg [msglen-2] = '+';
        msg [msglen-1] = '\0';
    }
    /* If exception_in_progress set, then no need to respond with another
     *  exception back to job manager. O/w, DO respond to job-manager
     *  and set exception-in-progress.
     */
    if (!job->exception_in_progress) {
        job->exception_in_progress = 1;
        if (jobinfo_respond_error (job, errnum, msg) < 0)
            flux_log_error (h, "jobinfo_fatal_verror: jobinfo_respond_error");
    }

    if (job->started) {
        jobinfo_cancel (job);
        return;
    }
    /* If job wasn't started, then finalize manually here */
    if (jobinfo_finalize (job) < 0) {
        flux_log_error (h, "jobinfo_fatal_verror: jobinfo_finalize");
        jobinfo_decref (job);
    }
}

void jobinfo_fatal_error (struct jobinfo *job,
                          int errnum,
                          const char *fmt, ...)
{
    flux_t *h = job->ctx->h;
    int saved_errno = errno;
    if (h && job->req) {
        va_list ap;
        va_start (ap, fmt);
        jobinfo_fatal_verror (job, errnum, fmt, ap);
        va_end (ap);
    }
    errno = saved_errno;
}

static void jobinfo_vraise (struct jobinfo *job,
                            const char *type,
                            int severity,
                            const char *fmt,
                            va_list ap)
{
    int n;
    char msg [256];
    int msglen = sizeof (msg);
    flux_t *h = job->ctx->h;

    if ((n = vsnprintf (msg, msglen, fmt, ap)) < 0)
        strcpy (msg, "vsnprintf error");
    else if (n >= msglen) {
        msg [msglen-2] = '+';
        msg [msglen-1] = '\0';
    }
    if (jobid_exception (h, job->id, job->req, type, severity, 0, msg) < 0)
        flux_log_error (h,
                        "error raising exception type=%s severity=%d: %s",
                        type,
                        severity,
                        msg);
}

void jobinfo_raise (struct jobinfo *job,
                    const char *type,
                    int severity,
                    const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    jobinfo_vraise (job, type, severity, fmt, ap);
    va_end (ap);
}

void jobinfo_log_output (struct jobinfo *job,
                         int rank,
                         const char *component,
                         const char *stream,
                         const char *data,
                         int len)
{
    char buf[16];
    if (len == 0 || !data || !stream)
        return;
    if (snprintf (buf, sizeof (buf), "%d", rank) >= sizeof (buf))
        flux_log_error (job->h, "jobinfo_log_output: snprintf");
    if (eventlogger_append_pack (job->ev, 0,
                                 "exec.eventlog",
                                 "log",
                                 "{ s:s, s:s s:s s:s# }",
                                 "component", component,
                                 "stream", stream,
                                 "rank", buf,
                                 "data", data, len) < 0)
        flux_log_error (job->h,
                        "eventlog_append failed: %s: message=%s",
                        idf58 (job->id),
                        data);
}

static void namespace_delete (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    flux_t *h = job->ctx->h;
    flux_future_t *fnext = flux_kvs_namespace_remove (h, job->ns);
    if (!fnext)
        flux_future_continue_error (f, errno, NULL);
    else
        flux_future_continue (f, fnext);
    flux_future_destroy (f);
}

static void namespace_copy (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    flux_t *h = job->ctx->h;
    flux_future_t *fnext = NULL;
    char dst [256];

    if (flux_job_kvs_key (dst, sizeof (dst), job->id, "guest") < 0) {
        flux_log_error (h, "namespace_move: flux_job_kvs_key");
        goto done;
    }
    if (!(fnext = flux_kvs_copy (h, job->ns, ".", NULL, dst, 0)))
        flux_log_error (h, "namespace_move: flux_kvs_copy");
done:
    if (fnext)
        flux_future_continue (f, fnext);
    else
        flux_future_continue_error (f, errno, NULL);
    flux_future_destroy (f);
}

/*  Move the guest namespace for `job` into the primary namespace first
 *   issuing the `done` terminating event into the exec.eventlog.
 *
 *  The process is split into a chained future of 3 parts:
 *   1. Issue the final write into the exec.eventlog
 *   2. Copy the namespace into the primary
 *   3. Delete the guest namespace
 */
static flux_future_t * namespace_move (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    flux_future_t *f = NULL;
    flux_future_t *f1 = NULL;
    flux_future_t *f2 = NULL;

    if (jobinfo_emit_event_pack_nowait (job, "done", NULL) < 0)
        flux_log_error (h, "emit_event");
    /*
     *  Ensure the final eventlog entry ("done", from above), is committed
     *   to then eventlog before performing the next steps. This ensures
     *   the eventlog is quiesced before the namespace is moved and becomes
     *   read-only.
     */
    if (!(f = eventlogger_commit (job->ev))) {
        flux_log_error (h, "namespace_move: jobinfo_emit_event");
        goto error;
    }
    if (!(f1 = flux_future_and_then (f, namespace_copy, job))
        || !(f1 = flux_future_or_then (f, namespace_copy, job))
        || !(f2 = flux_future_and_then (f1, namespace_delete, job))
        || !(f2 = flux_future_or_then (f1, namespace_delete, job))) {
        flux_log_error (h, "namespace_move: flux_future_and_then");
        goto error;
    }
    return f2;
error:
    flux_future_destroy (f);
    flux_future_destroy (f1);
    flux_future_destroy (f2);
    return NULL;
}

/* Notify job-exec that shells on ranks are complete.
 * XXX: currently assumes ranks equivalent to "all"
 */
void jobinfo_tasks_complete (struct jobinfo *job,
                             const struct idset *ranks,
                             int wait_status)
{
    assert (job->started == 1);
    if (wait_status > job->wait_status)
        job->wait_status = wait_status;

    /* XXX: ranks ignored for now until partial release supported
     *  only call jobinfo_complete() once all tasks have completed.
     */
    jobinfo_complete (job, ranks);

    if (jobinfo_finalize (job) < 0) {
        flux_log_error (job->h, "tasks_complete: jobinfo_finalize");
        jobinfo_decref (job);
    }
}


static int jobinfo_release (struct jobinfo *job)
{
    int rc = jobinfo_send_release (job, NULL);
    if (rc < 0) {
        flux_log_error (job->ctx->h, "jobinfo_send_release");
        jobinfo_respond_error (job, errno, "job release error");
    }
    /* Should be final destruction */
    jobinfo_decref (job);
    return rc;
}

static void namespace_move_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    jobinfo_release (job);
    flux_future_destroy (f);
}

/*
 *  All job shells have exited or we've hit an exception:
 *   start finalization steps:
 *   1. Move namespace into primary namespace, emitting final event to log
 */
static int jobinfo_finalize (struct jobinfo *job)
{
    flux_future_t *f = NULL;

    if (job->finalizing)
        return 0;
    job->finalizing = 1;

    if (job->has_namespace) {
        flux_future_t *f = namespace_move (job);
        if (!f || flux_future_then (f, -1., namespace_move_cb, job) < 0)
            goto error;
    }
    else if (jobinfo_release (job) < 0)
        goto error_release;
    return 0;
error:
    jobinfo_respond_error (job, errno, "finalize error");
error_release:
    flux_future_destroy (f);
    return -1;
}

static int jobinfo_start_execution (struct jobinfo *job)
{
    if (job->reattach)
        jobinfo_emit_event_pack_nowait (job, "re-starting", NULL);
    else
        jobinfo_emit_event_pack_nowait (job, "starting", NULL);
    /* Set started flag before calling 'start' method because we want to
     *  be sure to clean up properly if an exception occurs
     */
    job->started = 1;
    if ((*job->impl->start) (job) < 0) {
        jobinfo_fatal_error (job, errno, "%s: start failed", job->impl->name);
        return -1;
    }
    return 0;
}

static int jobinfo_load_implementation (struct jobinfo *job)
{
    int i = 0;
    int rc = -1;
    struct exec_implementation *impl;

    while ((impl = implementations[i]) && impl->name) {
        /*
         *  Immediately fail if any implementation init method returns < 0.
         *  If rc > 0, then select this implementation and skip others,
         *  O/w, continue with the list.
         */
        if ((rc = (*impl->init) (job)) < 0)
            return -1;
        else if (rc > 0) {
            job->impl = impl;
            return 0;
        }
        i++;
    }
    return -1;
}

/*  Completion for jobinfo_start_init (), finish init of jobinfo using
 *   data fetched from KVS
 */
static void jobinfo_start_continue (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;

    if (flux_future_get (flux_future_get_child (f, "ns"), NULL) < 0) {
        jobinfo_fatal_error (job, errno, "failed to create guest ns");
        goto done;
    }

    /*  If an exception was received during startup, no need to continue
     *   with startup
     */
    if (job->exception_in_progress)
        goto done;
    if (jobinfo_load_implementation (job) < 0) {
        jobinfo_fatal_error (job, errno, "failed to initialize implementation");
        goto done;
    }
    if (jobinfo_start_execution (job) < 0) {
        jobinfo_fatal_error (job, errno, "failed to start execution");
        goto done;
    }
done:
    jobinfo_decref (job); /* clear init reference */
    flux_future_destroy (f);
}

static flux_future_t * jobinfo_link_guestns (struct jobinfo *job)
{
    int saved_errno;
    flux_t *h = job->ctx->h;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    char key [64];

    if (flux_job_kvs_key (key, sizeof (key), job->id, "guest") < 0) {
        flux_log_error (h, "link guestns: flux_job_kvs_key");
        return NULL;
    }
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "link guestns: flux_kvs_txn_create");
        return NULL;
    }
    if (flux_kvs_txn_symlink (txn, 0, key, job->ns, ".") < 0) {
        flux_log_error (h, "link guestns: flux_kvs_txn_symlink");
        goto out;
    }
    if (!(f = flux_kvs_commit (h, NULL, 0, txn)))
        flux_log_error (h, "link_guestns: flux_kvs_commit");
out:
    saved_errno = errno;
    flux_kvs_txn_destroy (txn);
    errno = saved_errno;
    return f;
}

static void namespace_link (flux_future_t *fprev, void *arg)
{
    int saved_errno;
    flux_t *h = flux_future_get_flux (fprev);
    struct jobinfo *job = arg;
    flux_future_t *cf = NULL;
    flux_future_t *f = NULL;

    if (!(cf = flux_future_wait_all_create ())) {
        flux_log_error (h, "namespace_link: flux_future_wait_all_create");
        goto error;
    }
    flux_future_set_flux (cf, h);
    if (!(f = jobinfo_emit_event_pack (job,
                                       job->reattach ? "reattach" : "init",
                                       NULL))
        || flux_future_push (cf, "emit event", f) < 0)
        goto error;

    if (!(f = jobinfo_link_guestns (job))
        || flux_future_push (cf, "link guestns", f) < 0)
        goto error;
    flux_future_continue (fprev, cf);
    flux_future_destroy (fprev);
    return;
error:
    saved_errno = errno;
    flux_future_destroy (cf);
    flux_future_continue_error (fprev, saved_errno, NULL);
    flux_future_destroy (fprev);
}

static flux_future_t *ns_create_and_link (flux_t *h,
                                          struct jobinfo *job,
                                          int flags)
{
    flux_future_t *f = NULL;
    flux_future_t *f2 = NULL;

    if (job->reattach && job->rootref)
        f = flux_kvs_namespace_create_with (h,
                                            job->ns,
                                            job->rootref,
                                            job->userid,
                                            flags);
    else
        f = flux_kvs_namespace_create (h, job->ns, job->userid, flags);

    /*  Set job->has_namespace flag immediately after sending the namespace
     *  create RPC. This avoids the potential to leave orphaned namespaces
     *  if the job is canceled before the response is received.
     */
    job->has_namespace = 1;

    if (!f || !(f2 = flux_future_and_then (f, namespace_link, job))) {
        flux_log_error (h, "namespace_move: flux_future_and_then");
        flux_future_destroy (f);
        return NULL;
    }
    return f2;
}

static void get_rootref_cb (flux_future_t *fprev, void *arg)
{
    int saved_errno;
    flux_t *h = flux_future_get_flux (fprev);
    struct jobinfo *job = arg;
    flux_future_t *f = NULL;

    if (!(job->rootref = checkpoint_find_rootref (fprev,
                                                  job->id,
                                                  job->userid)))
        flux_log (job->h,
                  LOG_DEBUG,
                  "checkpoint rootref not found: %s",
                  idf58 (job->id));

    /* if rootref not found, still create namespace */
    if (!(f = ns_create_and_link (h, job, 0)))
        goto error;

    flux_future_continue (fprev, f);
    flux_future_destroy (fprev);
    return;
error:
    saved_errno = errno;
    flux_future_destroy (f);
    flux_future_continue_error (fprev, saved_errno, NULL);
    flux_future_destroy (fprev);
}

static flux_future_t *ns_get_rootref (flux_t *h,
                                      struct jobinfo *job,
                                      int flags)
{
    flux_future_t *f = NULL;
    flux_future_t *f2 = NULL;

    if (!(f = checkpoint_get_rootrefs (h))) {
        flux_log_error (h, "ns_get_rootref: checkpoint_get_rootrefs");
        return NULL;
    }
    if (!f || !(f2 = flux_future_and_then (f, get_rootref_cb, job))) {
        flux_log_error (h, "ns_get_rootref: flux_future_and_then");
        flux_future_destroy (f);
        return NULL;
    }
    return f2;
}

/*  Asynchronously fetch job data from KVS and create namespace.
 */
static flux_future_t *jobinfo_start_init (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    flux_future_t *f_kvs = NULL;
    flux_future_t *f = flux_future_wait_all_create ();
    flux_future_set_flux (f, job->ctx->h);

    if (job->reattach)
        f_kvs = ns_get_rootref (h, job, 0);
    else
        f_kvs = ns_create_and_link (h, job, 0);

    if (flux_future_push (f, "ns", f_kvs) < 0)
        goto err;

    return f;
err:
    flux_log_error (job->ctx->h, "jobinfo_kvs_lookup/namespace_create");
    flux_future_destroy (f_kvs);
    flux_future_destroy (f);
    return NULL;
}

static void evlog_err (struct eventlogger *ev, void *arg, int err, json_t *e)
{
    struct jobinfo *job = arg;
    char *s = json_dumps (e, JSON_COMPACT);
    flux_log_error (job->h,
                    "eventlog error: %s: entry=%s",
                    idf58 (job->id),
                    s);
    free (s);
}

static void evlog_busy (struct eventlogger *ev, void *arg)
{
    jobinfo_incref ((struct jobinfo *) arg);
}

static void evlog_idle (struct eventlogger *ev, void *arg)
{
    jobinfo_decref ((struct jobinfo *) arg);
}

static int job_start (struct job_exec_ctx *ctx, const flux_msg_t *msg)
{
    struct eventlogger_ops ev_ops = {
        .err = evlog_err,
        .busy = evlog_busy,
        .idle = evlog_idle
    };
    flux_future_t *f = NULL;
    struct jobinfo *job;
    json_t *R;

    if (!(job = jobinfo_new ()))
        return -1;


    /* Copy flux handle for each job to allow implementation access.
     * (This could also be done with an accessor, but choose the simpler
     *  approach for now)
     */
    job->h = ctx->h;
    job->kill_timeout = kill_timeout;

    job->req = flux_msg_incref (msg);

    job->ctx = ctx;

    if (flux_request_unpack (job->req,
                             NULL,
                             "{s:I s:i s:O s:b s:o}",
                             "id", &job->id,
                             "userid", &job->userid,
                             "jobspec", &job->jobspec,
                             "reattach", &job->reattach,
                             "R", &R) < 0) {
        flux_log_error (ctx->h, "start: flux_request_unpack");
        jobinfo_decref (job);
        return -1;
    }
    json_error_t error;
    if (!(job->R = resource_set_create_fromjson (R, &error))) {
        flux_log (ctx->h, LOG_ERR, "reading R: %s", error.text);
        jobinfo_decref (job);
        return -1;
    }
    size_t size = idset_count (resource_set_ranks (job->R));
    if (!(job->critical_ranks = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (job->critical_ranks, 0, size - 1) < 0) {
        flux_log_error (ctx->h, "initializing critical ranks");
        jobinfo_decref (job);
        return -1;
    }

    if (job->userid != getuid ())
        job->multiuser = 1;

    /*  Take a reference until initialization complete in case an
     *   exception is generated during this phase
     */
    jobinfo_incref (job);

    if (flux_job_kvs_namespace (job->ns, sizeof (job->ns), job->id) < 0) {
        jobinfo_fatal_error (job, errno, "failed to create ns name for job");
        flux_log_error (ctx->h, "job_ns_create");
        goto error;
    }
    job->ev = eventlogger_create (job->h, 0.01, &ev_ops, job);
    if (!job->ev || eventlogger_setns (job->ev, job->ns) < 0) {
        flux_log_error (job->h,
                        "eventlogger_create/setns for job %s failed",
                        idf58 (job->id));
        goto error;
    }

    if (zhashx_insert (ctx->jobs, &job->id, job) < 0) {
        errno = EEXIST;
        flux_log_error (ctx->h, "zhashx_insert");
        jobinfo_fatal_error (job, errno, "failed to hash job");
        goto error;
    }
    if (!(f = jobinfo_start_init (job))) {
        flux_log_error (ctx->h, "start: jobinfo_kvs_lookup");
        goto error;
    }
    if (flux_future_then (f, -1., jobinfo_start_continue, job) < 0) {
        flux_log_error (ctx->h, "start: flux_future_then");
        goto error;
    }
    return 0;
error:
    jobinfo_fatal_error (job, errno, "job start failure");
    jobinfo_decref (job);
    return -1;
}

static void start_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_exec_ctx *ctx = arg;

    if (job_start (ctx, msg) < 0) {
        flux_log_error (h, "job_start");
        /* The following "normal" RPC response will trigger the job-manager's
         * teardown of the exec system interface.
         */
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (h, "job-exec.start respond_error");
    }
}

static void exception_cb (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_exec_ctx *ctx = arg;
    flux_jobid_t id;
    int severity = 0;
    const char *type = NULL;
    struct jobinfo *job = NULL;

    if (flux_event_unpack (msg,
                           NULL,
                           "{s:I s:s s:i}",
                           "id", &id,
                           "type", &type,
                           "severity", &severity) < 0) {
        flux_log_error (h, "job-exception event");
        return;
    }
    if (severity == 0
        && (job = zhashx_lookup (ctx->jobs, &id))
        && (!job->finalizing)) {
        if (job->exception_in_progress) {
            /*  Resend term_signal even if exception in progress */
            (*job->impl->kill) (job, term_signal);
            return;
        }
        /*  !job->exception_in_progress:
         *
         *  Set job->exception_in_progress so that jobinfo_fatal_error()
         *   doesn't dump a duplicate exception into the eventlog.
         */
        job->exception_in_progress = 1;
        flux_log (h, LOG_DEBUG, "exec aborted: id=%s", idf58 (id));
        jobinfo_fatal_error (job, 0, "aborted due to exception type=%s", type);
    }
}

static void critical_ranks_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    struct job_exec_ctx *ctx = arg;
    flux_jobid_t id;
    const char *ranks;
    struct idset *idset;
    struct jobinfo *job;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:s}",
                             "id", &id,
                             "ranks", &ranks) < 0)
        goto error;

    if (!(job = zhashx_lookup (ctx->jobs, &id))) {
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_authorize (msg, job->userid) < 0
        || !(idset = idset_decode (ranks)))
        goto error;

    idset_destroy (job->critical_ranks);
    job->critical_ranks = idset;

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);

    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void expiration_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct job_exec_ctx *ctx = arg;
    flux_jobid_t id;
    double expiration;
    struct jobinfo *job;

    /*  Respond with error if module debug flag set for testing purposes
     */
    if (flux_module_debug_test (h, DEBUG_FAIL_EXPIRATION, false)) {
        errno = EINVAL;
        goto error;
    }
    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:F}",
                             "id", &id,
                             "expiration", &expiration) < 0)
        goto error;

    if (!(job = zhashx_lookup (ctx->jobs, &id))) {
        errno = ENOENT;
        goto error;
    }
    resource_set_update_expiration (job->R, expiration);
    if (jobinfo_set_expiration (job) < 0)
        goto error;
    flux_log (h,
              LOG_DEBUG,
              "updated expiration of %s to %.2f",
              idf58 (job->id),
              resource_set_expiration (job->R));
    if (flux_respond (ctx->h, msg, NULL) < 0)
        flux_log_error (h, "error responding to expiration update RPC");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "expiration_cb: flux_respond_error");
}

static void job_exec_ctx_destroy (struct job_exec_ctx *ctx)
{
    if (ctx == NULL)
        return;
    zhashx_destroy (&ctx->jobs);
    flux_msg_handler_delvec (ctx->handlers);
    free (ctx);
}

static struct job_exec_ctx * job_exec_ctx_create (flux_t *h,
                                                  int argc,
                                                  char **argv)
{
    struct job_exec_ctx *ctx = calloc (1, sizeof (*ctx));
    if (ctx == NULL)
        return NULL;
    ctx->h = h;
    ctx->argc = argc;
    ctx->argv = argv;
    if (!(ctx->jobs = job_hash_create ())) {
        ERRNO_SAFE_WRAP (free, ctx);
        return NULL;
    }
    return (ctx);
}

static int exec_hello (flux_t *h, const char *service)
{
    int rc = -1;
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h, "job-manager.exec-hello",
                             FLUX_NODEID_ANY, 0,
                             "{s:s}",
                             "service", service))) {
        flux_log_error (h, "flux_rpc (job-manager.exec-hello)");
        return -1;
    }
    if ((rc = flux_future_get (f, NULL)) < 0)
        flux_log_error (h, "job-manager.exec-hello");
    flux_future_destroy (f);
    return rc;
}

/*  Initialize job-exec module global config from defaults, config, cmdline,
 *   in that order.
 */
static int job_exec_set_config_globals (flux_t *h,
                                        const flux_conf_t *conf,
                                        int argc,
                                        char **argv,
                                        flux_error_t *errp)
{
    const char *kto = NULL;
    const char *tsignal = NULL;
    const char *ksignal = NULL;
    flux_error_t error;

    /* Per trws comment in 97421e88987535260b10d6a19551cea625f26ce4
     *
     * The musl libc loader doesn't actually unload objects on
     * dlclose, so a subsequent dlopen doesn't re-clear globals and
     * similar.
     *
     * So we must re-initialize globals everytime we reload the module.
     */
    kill_timeout = 5.0;
    max_kill_count = 8;
    term_signal = SIGTERM;
    kill_signal = SIGKILL;

    if (flux_conf_unpack (conf,
                          &error,
                          "{s?{s?s s?s s?s s?i}}",
                          "exec",
                            "kill-timeout", &kto,
                            "term-signal", &tsignal,
                            "kill-signal", &ksignal,
                            "max-kill-count", &max_kill_count) < 0)
        return errprintf (errp,
                          "Error reading [exec] table: %s",
                          error.text);

    /* Override via commandline */
    for (int i = 0; i < argc; i++) {
        if (strstarts (argv[i], "kill-timeout="))
            kto = argv[i] + 13;
        else if (strstarts (argv[i], "kill-signal="))
            ksignal = argv[i] + 12;
        else if (strstarts (argv[i], "term-signal="))
            tsignal = argv[i] + 12;
        else if (strstarts (argv[i], "max-kill-count="))
            max_kill_count = atoi(argv[i] + 15);
    }

    if (kto) {
        if (fsd_parse_duration (kto, &kill_timeout) < 0) {
            errprintf (errp, "invalid kill-timeout: %s", kto);
            errno = EINVAL;
            return -1;
        }
    }
    if (ksignal) {
        if ((kill_signal = sigutil_signum (ksignal)) < 0) {
            errprintf (errp, "invalid kill-signal: %s", ksignal);
            errno = EINVAL;
            return -1;
        }
    }
    if (tsignal) {
        if ((term_signal = sigutil_signum (tsignal)) < 0) {
            errprintf (errp, "invalid term-signal: %s", tsignal);
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

static int configure_implementations (flux_t *h, int argc, char **argv)
{
    struct exec_implementation *impl;
    const flux_conf_t *conf;
    flux_error_t err;
    int i = 0;
    if (!(conf = flux_get_conf (h))) {
        flux_log_error (h, "error retrieving flux conf");
        return -1;
    }
    while ((impl = implementations[i]) && impl->name) {
        if (impl->config && (*impl->config) (h, conf, argc, argv, &err) < 0) {
            flux_log (h, LOG_ERR, "%s", err.text);
            return -1;
        }
        i++;
    }
    return 0;
}

static int remove_running_ns (struct job_exec_ctx *ctx)
{
    struct jobinfo *job = zhashx_first (ctx->jobs);
    flux_future_t *fall = NULL;
    flux_future_t *f = NULL;
    int rv = -1;

    while (job) {
        if (job->running) {
            if (!fall) {
                if (!(fall = flux_future_wait_all_create ()))
                    goto cleanup;
                flux_future_set_flux (fall, ctx->h);
            }
            if (!(f = flux_kvs_namespace_remove (ctx->h, job->ns)))
                goto cleanup;
            if (flux_future_push (fall, job->ns, f) < 0)
                goto cleanup;
            f = NULL;
        }
        job = zhashx_next (ctx->jobs);
    }

    if (fall) {
        if (flux_future_wait_for (fall, -1.) < 0)
            goto cleanup;
    }

    rv = 0;
cleanup:
    flux_future_destroy (f);
    flux_future_destroy (fall);
    return rv;
}

static int unload_implementations (struct job_exec_ctx *ctx)
{
    struct exec_implementation *impl;
    int i = 0;
    if (ctx && ctx->jobs) {
        checkpoint_running (ctx->h, ctx->jobs);
        if (remove_running_ns (ctx) < 0)
            flux_log_error (ctx->h, "failed to remove guest namespaces");
    }
    while ((impl = implementations[i]) && impl->name) {
        if (impl->unload)
             (*impl->unload) ();
        i++;
    }
    return 0;
}

static json_t *running_job_stats (struct job_exec_ctx *ctx)
{
    struct jobinfo *job = zhashx_first (ctx->jobs);
    json_t *o = NULL;

    if (!(o = json_object ())) {
        errno = ENOMEM;
        return NULL;
    }
    while (job) {
        json_t *entry;
        char *critical_ranks;
        json_t *impl_stats = NULL;

        if (!(critical_ranks = idset_encode (job->critical_ranks,
                                             IDSET_FLAG_RANGE)))
            goto error;

        entry = json_pack ("{s:s s:s s:s s:i s:i s:i s:i s:i s:i s:f s:i s:i}",
                           "implementation",
                           job->impl ? job->impl->name : "none",
                           "ns", job->ns,
                           "critical_ranks", critical_ranks,
                           "multiuser", job->multiuser,
                           "has_namespace", job->has_namespace,
                           "exception_in_progress", job->exception_in_progress,
                           "started", job->started,
                           "running", job->running,
                           "finalizing", job->finalizing,
                           "kill_timeout", job->kill_timeout,
                           "kill_count", job->kill_count,
                           "kill_shell_count", job->kill_shell_count);

        free (critical_ranks);
        if (!entry) {
            errno = ENOMEM;
            goto error;
        }
        if (job->impl
            && job->impl->stats
            && (impl_stats = (*job->impl->stats) (job))
            && json_object_update_missing (entry, impl_stats) < 0) {
            json_decref (entry);
            json_decref (impl_stats);
            errno = ENOMEM;
            goto error;
        }
        json_decref (impl_stats);
        if (json_object_set_new (o, idf58 (job->id), entry) < 0) {
            json_decref (entry);
            errno = ENOMEM;
            goto error;
        }
        job = zhashx_next (ctx->jobs);
    }
    return o;
error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

static void stats_cb (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_exec_ctx *ctx = arg;
    struct exec_implementation *impl;
    json_t *o = NULL;
    json_t *jobs;
    int i = 0;

    if (!(o = json_pack ("{s:f s:s s:s s:i}",
                         "kill-timeout", kill_timeout,
                         "term-signal", sigutil_signame (term_signal),
                         "kill-signal", sigutil_signame (kill_signal),
                         "max-kill-count", max_kill_count))) {
        errno = ENOMEM;
        goto error;
    }
    if (!(jobs = running_job_stats (ctx))
        || json_object_set_new (o, "jobs", jobs)) {
        json_decref (jobs);
        errno = ENOMEM;
        goto error;
    }
    while ((impl = implementations[i]) && impl->name) {
        json_t *stats = NULL;
        if (impl->stats && (stats = (*impl->stats) (NULL))) {
            if (json_object_set_new (o, impl->name, stats) < 0) {
                json_decref (stats);
                errno = ENOMEM;
                goto error;
            }
        }
        i++;
    }
    if (flux_respond_pack (h, msg, "o", o) < 0)
        flux_log_error (h, "error responding to stats-get request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to stats-get request");
    json_decref (o);
}

static void config_reload_cb (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct job_exec_ctx *ctx = arg;
    const flux_conf_t *conf;
    struct exec_implementation *impl;
    flux_error_t err;
    int i = 0;

    if (flux_conf_reload_decode (msg, &conf) < 0) {
        errprintf (&err, "Error parsing new config: %s", strerror (errno));
        goto error;
    }

    if (job_exec_set_config_globals (h,
                                     conf,
                                     ctx->argc,
                                     ctx->argv,
                                     &err) < 0)
        goto error;

    while ((impl = implementations[i]) && impl->name) {
        if (impl->config) {
            if ((*impl->config) (h, conf, ctx->argc, ctx->argv, &err) < 0)
                goto error;
        }
        i++;
    }
    if (flux_set_conf (h, flux_conf_incref (conf)) < 0) {
        errprintf (&err, "error updating cached configuration");
        flux_conf_decref (conf);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to config-reload request");
    return;
error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "error responding to config-reload request");
}

static const struct flux_msg_handler_spec htab[]  = {
    { FLUX_MSGTYPE_REQUEST, "job-exec.start", start_cb,     0 },
    { FLUX_MSGTYPE_EVENT,   "job-exception",  exception_cb, 0 },
    { FLUX_MSGTYPE_REQUEST,
      "job-exec.critical-ranks",
       critical_ranks_cb,
       FLUX_ROLE_USER
    },
    { FLUX_MSGTYPE_REQUEST, "job-exec.expiration", expiration_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job-exec.stats-get", stats_cb, 0 },
    { FLUX_MSGTYPE_REQUEST, "job-exec.config-reload", config_reload_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int saved_errno = 0;
    int rc = -1;
    bool subscribed = false;
    flux_error_t error;
    struct job_exec_ctx *ctx = job_exec_ctx_create (h, argc, argv);

    if (job_exec_set_config_globals (h,
                                     flux_get_conf (h),
                                     argc,
                                     argv,
                                     &error) < 0) {
        flux_log_error (h, "job-exec: error parsing config: %s", error.text);
        goto out;
    }
    if (configure_implementations (h, argc, argv) < 0) {
        flux_log_error (h, "job-exec: module initialization failed");
        goto out;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto out;
    }
    if (flux_event_subscribe (h, "job-exception") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto out;
    }
    subscribed = true;
    if (exec_hello (h, "job-exec") < 0)
        goto out;

    rc = flux_reactor_run (flux_get_reactor (h), 0);
out:
    unload_implementations (ctx);

    saved_errno = errno;
    if (subscribed && flux_event_unsubscribe (h, "job-exception") < 0)
        flux_log_error (h, "flux_event_unsubscribe ('job-exception')");
    job_exec_ctx_destroy (ctx);
    errno = saved_errno;
    return rc;
}

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
