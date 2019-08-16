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
 * simulate an epilog/cleanup stage, and/or mock exceptions during run
 * or initialization. See TEST CONFIGURATION below.
 *
 * OPERATION
 *
 * For details of startup protocol, see job-manager/start.c.
 *
 * JOB INIT:
 *
 * On reciept of a start request, the exec service enters initialization
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
 * event to the exec eventlog and calls the implementaton "start" method.
 * Once all job shells or equivalent are running, the exec implementation
 * should invoke jobinfo_started(), which emits a "running" event to the
 * exec eventlog and sends the "start" response to the job-manager.
 *
 * JOB FINISH/CLEANUP:
 *
 * As tasks/job shells exit, the exec implementation should call
 * jobinfo_tasks_complete(), which emits a "complete" event to the exec
 * eventlog, sends a "finish" response to the job-manager, emits a
 * "cleanup.start" event in the exec eventlog, and finally invokes
 * the exec implementation's "cleanup" method on the completed ranks.
 * (NB: currently a subset of ranks is not supported)
 *
 * JOB FINALIZATION:
 *
 * Once cleanup tasks have completed, the exec implementation should call
 * jobinfo_cleanup_complete(), which emits "cleanup.finish" to the exec
 * eventlog, and then calls jobinfo_finalize, which performs the following
 * tasks:
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
 *   "cleanup_duration":s   - enable a fake job epilog and set its duration
 *   "wait_status":i        - report this value as status in the "finish" resp
 *   "mock_exception":s     - mock an exception during this phase of job
 *                             execution (currently "init" and "run")
 * }
 *
 * The "bulk" execution implementation supports testing and other
 * paramters under attributes.system.exec.bulkexec, including:
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
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/fsd.h"
#include "job-exec.h"

#define DEFAULT_KILL_TIMEOUT 5.0

extern struct exec_implementation testexec;
extern struct exec_implementation bulkexec;

static struct exec_implementation * implementations[] = {
    &testexec,
    &bulkexec,
    NULL
};

struct job_exec_ctx {
    flux_t *              h;
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
        flux_watcher_destroy (job->kill_timer);
        zhashx_delete (job->ctx->jobs, &job->id);
        if (job->impl && job->impl->exit)
            (*job->impl->exit) (job);
        job->ctx = NULL;
        flux_msg_destroy (job->req);
        job->req = NULL;
        resource_set_destroy (job->R);
        json_decref (job->jobspec);
        free (job);
        errno = saved_errno;
    }
}

static struct jobinfo * jobinfo_new (void)
{
    struct jobinfo *job = calloc (1, sizeof (*job));
    job->refcount = 1;
    job->kill_timeout = DEFAULT_KILL_TIMEOUT;
    return job;
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

static void emit_event_continuation (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (flux_future_get (f, NULL) < 0)
        flux_log_error (job->ctx->h, "%ju: emit_event", job->id);
    flux_future_destroy (f);
    jobinfo_decref (job);
}

static int jobinfo_emit_event_vpack_nowait (struct jobinfo *job,
                                            const char *name,
                                            const char *fmt,
                                            va_list ap)
{
    flux_future_t *f = jobinfo_emit_event_vpack (job, name, fmt, ap);
    if (f == NULL)
        return -1;
    jobinfo_incref (job);
    if (flux_future_then (f, -1., emit_event_continuation, job) < 0) {
        flux_log_error (job->ctx->h, "jobinfo_emit_event");
        goto error;
    }
    return 0;
error:
    jobinfo_decref (job);
    flux_future_destroy (f);
    return -1;

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

static int jobid_respond_error (flux_t *h, flux_jobid_t id,
                                const flux_msg_t *msg,
                                int errnum, const char *text)
{
    char note [256];
    if (errnum)
        snprintf (note, sizeof (note), "%s%s%s",
                                        text ? text : "",
                                        text ? ": " : "",
                                        strerror (errnum));
    else
        snprintf (note, sizeof (note), "%s", text ? text : "");
    return flux_respond_pack (h, msg, "{s:I s:s s:{s:i s:s s:s}}",
                                      "id", id,
                                      "type", "exception",
                                      "data",
                                      "severity", 0,
                                      "type", "exec",
                                      "note", note);
}

static int jobinfo_respond_error (struct jobinfo *job, int errnum,
                                  const char *msg)
{
    return jobid_respond_error (job->ctx->h, job->id, job->req, errnum, msg);
}

static int jobinfo_send_release (struct jobinfo *job,
                                 const struct idset *idset)
{
    int rc;
    flux_t *h = job->ctx->h;
    // XXX: idset ignored for now. Always release all resources
    rc = flux_respond_pack (h, job->req, "{s:I s:s s{s:s s:b}}",
                                         "id", job->id,
                                         "type", "release",
                                         "data", "ranks", "all",
                                                 "final", true);
    return rc;
}

static int jobinfo_respond (flux_t *h, struct jobinfo *job,
                            const char *event, int status)
{
    return flux_respond_pack (h, job->req, "{s:I s:s s:{}}",
                                           "id", job->id,
                                           "type", event,
                                           "data");
}

static void jobinfo_complete (struct jobinfo *job, const struct idset *ranks)
{
    flux_t *h = job->ctx->h;
    job->running = 0;
    if (h && job->req) {
        jobinfo_emit_event_pack_nowait (job, "complete",
                                        "{ s:i }",
                                        "status", job->wait_status);
        if (flux_respond_pack (h, job->req, "{s:I s:s s:{s:i}}",
                                            "id", job->id,
                                            "type", "finish",
                                            "data",
                                            "status", job->wait_status) < 0)
            flux_log_error (h, "jobinfo_complete: flux_respond");
    }
}

void jobinfo_started (struct jobinfo *job, const char *fmt, ...)
{
    flux_t *h = job->ctx->h;
    if (h && job->req) {
        va_list ap;
        va_start (ap, fmt);
        jobinfo_emit_event_vpack_nowait (job, "running", fmt, ap);
        job->running = 1;
        va_end (ap);
        if (jobinfo_respond (h, job, "start", 0) < 0)
            flux_log_error (h, "jobinfo_started: flux_respond");
    }
}

static void kill_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                           int revents, void *arg)
{
    struct jobinfo *job = arg;
    flux_log (job->h,
              LOG_DEBUG,
              "Sending SIGKILL to job %ju",
              (uintmax_t) job->id);
    (*job->impl->kill) (job, SIGKILL);
}

/*  Cancel any pending shells to execute with implementations cancel
 *   method, send SIGTERM to executing shells to notify them to terminate,
 *   schedule SIGKILL to be sent after kill_timeout seconds.
 */
static void jobinfo_cancel (struct jobinfo *job)
{
    if (job->impl->cancel)
        (*job->impl->cancel) (job);

    (*job->impl->kill) (job, SIGTERM);
    job->kill_timer = flux_timer_watcher_create (flux_get_reactor (job->h),
                                                 job->kill_timeout, 0.,
                                                 kill_timer_cb, job);
    flux_watcher_start (job->kill_timer);
}

static int jobinfo_finalize (struct jobinfo *job);

static void jobinfo_fatal_verror (struct jobinfo *job, int errnum,
                                  const char *fmt, va_list ap)
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
    jobinfo_emit_event_pack_nowait (job, "exception", "{ s:s }", "note", msg);
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

void jobinfo_fatal_error (struct jobinfo *job, int errnum,
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
    char dst [265];

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
    flux_future_t *fnext = NULL;

    if (!(f = jobinfo_emit_event_pack (job, "done", NULL))) {
        flux_log_error (h, "namespace_move: jobinfo_emit_event");
        goto error;
    }
    if (   !(fnext = flux_future_and_then (f, namespace_copy, job))
        || !(fnext = flux_future_and_then (f=fnext, namespace_delete, job))) {
        flux_log_error (h, "namespace_move: flux_future_and_then");
        goto error;
    }
    return fnext;
error:
    flux_future_destroy (f);
    flux_future_destroy (fnext);
    return NULL;
}

static void cleanup_complete_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (flux_future_get (f, NULL) < 0)
        flux_log_error (job->h, "cleanup_complete_cb: event_pack");
    flux_future_destroy (f);

    /* XXX: when cleanup ranks are tracked, only finalize once all
     *  involved ranks have completed cleanup. For now though, only
     *  one cleanup_complete call is expected.
     */
    if (jobinfo_finalize (job) < 0) {
        flux_log_error (job->h, "cleanup_complete_cb: jobinfo_finalize");
        jobinfo_decref (job);
    }
}

/* Notify job-exec that any "cleanup" tasks including epilog have
 *  completed on ranks with return code `rc`.
 *
 * XXX: ranks ignored for now
 */
void jobinfo_cleanup_complete (struct jobinfo *job,
                               const struct idset *ranks,
                               int rc)
{
    flux_future_t *f = NULL;

    /*  XXX: It isn't clear what to do if a cleanup task fails.
     *   For now, log the return code from the cleanup composite
     *   future and errno if rc < 0 for informational purposes,
     *   but do not generate an exception.
     */
    if (rc < 0)
        f = jobinfo_emit_event_pack (job, "cleanup.finish",
                                     "{ s:s s:i s:s }",
                                     "ranks", "all",
                                     "rc", rc,
                                     "note", strerror (errno));
    else
        f = jobinfo_emit_event_pack (job, "cleanup.finish",
                                     "{ s:s s:i }",
                                     "ranks", "all",
                                     "rc", rc);
   if (flux_future_then (f, -1., cleanup_complete_cb, job) < 0) {
        jobinfo_respond_error (job, errno, "cleanup complete error");
        flux_future_destroy (f);
    }
}

static void jobinfo_start_cleanup_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;

    /*  Log error if cleanup.start event failed */
    if (flux_future_get (f, NULL) < 0)
        flux_log_error (job->h, "jobinfo_emit_event (cleanup.start)");
    flux_future_destroy (f);

    if ((*job->impl->cleanup) (job, NULL) < 0)
        flux_log_error (job->h, "%s: cleanup()", job->impl->name);
}

/*  Initiate cleanup on ranks in idset, if necessary */
static void jobinfo_start_cleanup (struct jobinfo *job,
                                   const struct idset *idset)
{
    flux_future_t *f = NULL;

    /* XXX: idset ignored for now */

    if (!(f = jobinfo_emit_event_pack (job, "cleanup.start",
                                            "{ s:s }",
                                            "ranks", "all"))) {
        flux_log_error (job->h, "jobinfo_emit_event_pack");
        goto error;
    }
    if (flux_future_then (f, -1., jobinfo_start_cleanup_cb, job) < 0) {
        flux_log_error (job->h, "flux_future_then");
        goto error;
    }
    return;
error:
    jobinfo_respond_error (job, errno, "cleanup start error");
    flux_future_destroy (f);
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

    /* Start cleanup tasks on completed ranks */
    jobinfo_start_cleanup (job, ranks);
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
 *   1. Ensure all cleanup tasks have completed
 *   2. Move namespace into primary namespace, emitting final event to log
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

/*  Lookup key 'key' under jobid 'id' kvs dir:
 */
static flux_future_t *flux_jobid_kvs_lookup (flux_t *h, flux_jobid_t id,
                                             int flags, const char *key)
{
    char path [256];
    if (flux_job_kvs_key (path, sizeof (path), id, key) < 0)
        return NULL;
    return flux_kvs_lookup (h, NULL, flags, path);
}

/*
 *  Call lookup_get on a child named 'name' of the composite future 'f'
 */
static const char * jobinfo_kvs_lookup_get (flux_future_t *f, const char *name)
{
    const char *result;
    flux_future_t *child = flux_future_get_child (f, name);
    if (child == NULL)
        return NULL;
    if (flux_kvs_lookup_get (child, &result) < 0)
        return NULL;
    return result;
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

/*  Completion for jobinfo_initialize(), finish init of jobinfo using
 *   data fetched from KVS
 */
static void jobinfo_start_continue (flux_future_t *f, void *arg)
{
    json_error_t error;
    const char *R = NULL;
    const char *jobspec = NULL;
    struct jobinfo *job = arg;

    if (flux_future_get (flux_future_get_child (f, "ns"), NULL) < 0) {
        jobinfo_fatal_error (job, errno, "failed to create guest ns");
        goto done;
    }
    job->has_namespace = 1;

    /*  If an exception was received during startup, no need to continue
     *   with startup
     */
    if (job->exception_in_progress)
        goto done;

    if (!(jobspec = jobinfo_kvs_lookup_get (f, "jobspec"))) {
        jobinfo_fatal_error (job, errno, "unable to fetch jobspec");
        goto done;
    }
    if (!(R = jobinfo_kvs_lookup_get (f, "R"))) {
        jobinfo_fatal_error (job, errno, "job does not have allocation");
        goto done;
    }
    if (!(job->R = resource_set_create (R, &error))) {
        jobinfo_fatal_error (job, errno, "reading R: %s", error.text);
        goto done;
    }
    if (!(job->jobspec = json_loads (jobspec, 0, &error))) {
        jobinfo_fatal_error (job, errno, "reading jobspec: %s", error.text);
        goto done;
    }
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
    if (!(f = jobinfo_emit_event_pack (job, "init", NULL))
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

    if (!(f = flux_kvs_namespace_create (h, job->ns, job->userid, flags))
        || !(f2 = flux_future_and_then (f, namespace_link, job))) {
        flux_log_error (h, "namespace_move: flux_future_and_then");
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

    if (!(f_kvs = flux_jobid_kvs_lookup (h, job->id, 0, "R"))
        || flux_future_push (f, "R", f_kvs) < 0)
        goto err;
    if (!(f_kvs = flux_jobid_kvs_lookup (h, job->id, 0, "jobspec"))
        || flux_future_push (f, "jobspec", f_kvs) < 0)
        goto err;
    if (!(f_kvs = ns_create_and_link (h, job, 0))
        || flux_future_push (f, "ns", f_kvs))
        goto err;

    return f;
err:
    flux_log_error (job->ctx->h, "jobinfo_kvs_lookup/namespace_create");
    flux_future_destroy (f);
    return NULL;
}

static double job_get_kill_timeout (flux_t *h)
{
    double t = DEFAULT_KILL_TIMEOUT;
    const char *kto = flux_attr_get (h, "job-exec.kill_timeout");
    if (kto && fsd_parse_duration (kto, &t) < 0)
        flux_log_error (h, "job-exec.kill_timeout=%s", kto);
    return t;
}

static int job_start (struct job_exec_ctx *ctx, const flux_msg_t *msg)
{
    flux_future_t *f = NULL;
    struct jobinfo *job;

    if (!(job = jobinfo_new ()))
        return -1;

    /*  Take a reference until initialization complete in case an
     *   exception is generated during this phase
     */
    jobinfo_incref (job);

    /* Copy flux handle for each job to allow implementation access.
     * (This could also be done with an accessor, but choose the simpler
     *  approach for now)
     */
    job->h = ctx->h;
    job->kill_timeout = job_get_kill_timeout (job->h);

    if (!(job->req = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "start: flux_msg_copy");
        if (flux_respond_error (ctx->h, msg, errno, "flux_msg_copy failed") < 0)
            flux_log_error (ctx->h, "flux_respond_error");
        jobinfo_decref (job);
        return -1;
    }
    job->ctx = ctx;

    if (flux_request_unpack (job->req, NULL, "{s:I, s:i}",
                                             "id", &job->id,
                                             "userid", &job->userid) < 0) {
        flux_log_error (ctx->h, "start: flux_request_unpack");
        jobinfo_decref (job);
        return -1;
    }
    if (flux_job_kvs_namespace (job->ns, sizeof (job->ns), job->id) < 0) {
        jobinfo_fatal_error (job, errno, "failed to create ns name for job");
        flux_log_error (ctx->h, "job_ns_create");
        return -1;
    }
    if (zhashx_insert (ctx->jobs, &job->id, job) < 0) {
        flux_log_error (ctx->h, "zhashx_insert");
        jobinfo_fatal_error (job, errno, "failed to hash job");
        return -1;
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
    return -1;
}

static void start_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
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

static void exception_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct job_exec_ctx *ctx = arg;
    flux_jobid_t id;
    int severity = 0;
    const char *type = NULL;
    struct jobinfo *job = NULL;

    if (flux_event_unpack (msg, NULL, "{s:I s:s s:i}",
                                      "id", &id,
                                      "type", &type,
                                      "severity", &severity) < 0) {
        flux_log_error (h, "job-exception event");
        return;
    }
    if (severity == 0
        && (job = zhashx_lookup (ctx->jobs, &id))
        && (!job->finalizing)
        && (!job->exception_in_progress)) {
        job->exception_in_progress = 1;
        flux_log (h, LOG_DEBUG, "exec aborted: id=%ld", id);
        jobinfo_fatal_error (job, 0, "aborted due to exception type=%s", type);
    }
}

static size_t job_hash_fn (const void *key)
{
    const flux_jobid_t *id = key;
    return *id;
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

static int job_hash_key_cmp (const void *x, const void *y)
{
    const flux_jobid_t *id1 = x;
    const flux_jobid_t *id2 = y;

    return NUMCMP (*id1, *id2);
}

static void job_exec_ctx_destroy (struct job_exec_ctx *ctx)
{
    if (ctx == NULL)
        return;
    zhashx_destroy (&ctx->jobs);
    flux_msg_handler_delvec (ctx->handlers);
    free (ctx);
}

static struct job_exec_ctx * job_exec_ctx_create (flux_t *h)
{
    struct job_exec_ctx *ctx = calloc (1, sizeof (*ctx));
    if (ctx == NULL)
        return NULL;
    ctx->h = h;
    ctx->jobs = zhashx_new ();
    zhashx_set_key_hasher (ctx->jobs, job_hash_fn);
    zhashx_set_key_comparator (ctx->jobs, job_hash_key_cmp);
    zhashx_set_key_duplicator (ctx->jobs, NULL);
    zhashx_set_key_destructor (ctx->jobs, NULL);
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

static const struct flux_msg_handler_spec htab[]  = {
    { FLUX_MSGTYPE_REQUEST, "job-exec.start", start_cb,     0 },
    { FLUX_MSGTYPE_EVENT,   "job-exception",  exception_cb, 0 },
    FLUX_MSGHANDLER_TABLE_END
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    struct job_exec_ctx *ctx = job_exec_ctx_create (h);

    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto out;
    }
    if (flux_event_subscribe (h, "job-exception") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto out;
    }
    if (exec_hello (h, "job-exec") < 0)
        goto out;

    rc = flux_reactor_run (flux_get_reactor (h), 0);
out:
    if (flux_event_unsubscribe (h, "job-exception") < 0)
        flux_log_error (h, "flux_event_unsubscribe ('job-exception')");
    job_exec_ctx_destroy (ctx);
    return rc;
}

MOD_NAME ("job-exec");

/*
 * vi: tabstop=4 shiftwidth=4 expandtab
 */
