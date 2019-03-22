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
 * For deatils of startup protocol, see job-manager/start.c.
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
 * complete. If any of these steps fail, or a mock exception is configured
 * for "init", an exec initialization exception * is thrown.
 *
 * JOB STARTING/RUNNING:
 *
 * The current exec service fakes a running job by initiating a timer for
 * the configured duration of the job, or 10us by default. The "start"
 * response to the job manager is sent just before the timer is started,
 * to simulate the condition when all job shells have been launched.
 *
 * JOB FINISH/CLEANUP:
 *
 * When the timer callback fires, then a "finish" response is sent to
 * the job-manager (with status set by TEST CONFIGURATION), and any
 * configured "cleanup" tasks are initiated. By default, no cleanup work
 * is configured unless the attributes.system.exec.test.cleanup_duration
 * key is set in the jobspec.  This simulates a "job epilog" that takes
 * some amount of time.
 *
 * JOB FINALIZATION:
 *
 * Once optional cleanup tasks have completed, the job is "finalized", which
 * includes the following steps, in order:
 *
 *  - terminating "done" event is posted to the exec.eventlog
 *  - the guest namespace, now quiesced, is copied to the primary namespace
 *  - the guest namespace is removed
 *  - the final "release final=1" response is sent to the job manager
 *  - the local job object is destroyed
 *
 * TEST CONFIGURATION
 *
 * The job-exec module supports an object in the jobspec under
 * attributes.system.exec.test, which supports the following keys
 *
 * {
 *   "run_duration":s,      - alternate/override attributes.system.duration
 *   "cleanup_duration":s   - enable a fake job epilog and set its duration
 *   "wait_status":i        - report this value as status in the "finish" resp
 *   "mock_exception":s     - mock an exception during this phase of job
 *                             execution (currently "init" and "run")
 * }
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libutil/fluid.h"
#include "src/common/libutil/fsd.h"
#include "rset.h"

struct job_exec_ctx {
    flux_t *              h;
    flux_msg_handler_t ** handlers;
    zhashx_t *            jobs;
};


/*  Exec system testing configuration:
 *  Set from jobspec attributes.system.exec.test object, if any.
 */
struct testconf {
    double                run_duration;     /* duration of fake job in sec  */
    double                cleanup_duration; /* if > 0., duration of epilog  */
    int                   wait_status;      /* reported status for "finish" */
    const char *          mock_exception;   /* fake excetion at this site   */
                                            /* ("init", or "run")           */
};

struct jobinfo {
    flux_jobid_t          id;
    char                  ns [64];
    flux_msg_t *          req;
    uint32_t              userid;
    int                   flags;

    struct resource_set * R;
    json_t *              jobspec;

    uint8_t               needs_cleanup:1;
    uint8_t               has_namespace:1;
    uint8_t               exception_in_progress:1;
    uint8_t               running:1;
    uint8_t               finalizing:1;

    int                   wait_status;

    int                   refcount;

    struct testconf       testconf;
    flux_watcher_t *      timer;

    zhashx_t *            cleanup;
    struct job_exec_ctx * ctx;
};

typedef flux_future_t * (*cleanup_task_f) (struct jobinfo *job);

static void jobinfo_incref (struct jobinfo *job)
{
    job->refcount++;
}

static void jobinfo_decref (struct jobinfo *job)
{
    if (job && (--job->refcount == 0)) {
        int saved_errno = errno;
        zhashx_delete (job->ctx->jobs, &job->id);
        job->ctx = NULL;
        flux_msg_destroy (job->req);
        job->req = NULL;
        resource_set_destroy (job->R);
        json_decref (job->jobspec);
        flux_watcher_destroy (job->timer);
        zhashx_destroy (&job->cleanup);
        free (job);
        errno = saved_errno;
    }
}

static struct jobinfo * jobinfo_new (void)
{
    struct jobinfo *job = calloc (1, sizeof (*job));
    job->cleanup = zhashx_new ();
    job->refcount = 1;
    return job;
}

static int ev_context_vsprintf (char *context, size_t len,
                                const char *fmt, va_list ap)
{
    int n;
    if (fmt == NULL)
        context[0] = '\0';
    else if ((n = vsnprintf (context, len, fmt, ap)) < 0)
        return -1;
    else if (n >= len) {
        context [len-2] = '+';
        context [len-1] = '\0';
    }
    return 0;
}

/*  Emit an event to the exec system eventlog and return a future from
 *   flux_kvs_commit().
 */
static flux_future_t * jobinfo_emit_eventv (struct jobinfo *job,
                                            const char *name,
                                            const char *fmt,
                                            va_list ap)
{
    int saved_errno;
    flux_t *h = job->ctx->h;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f = NULL;
    char context [256];
    char *event = NULL;
    const char *key = "exec.eventlog";

    if (ev_context_vsprintf (context, sizeof (context), fmt, ap) < 0) {
        flux_log_error (h, "emit event: ev_context_vsprintf");
        return NULL;
    }
    if (!(event = flux_kvs_event_encode (name, context))) {
        flux_log_error (h, "emit event: flux_kvs_event_encode");
        return NULL;
    }
    if (!(txn = flux_kvs_txn_create ())) {
        flux_log_error (h, "emit event: flux_kvs_txn_create");
        goto out;
    }
    if (flux_kvs_txn_put (txn, FLUX_KVS_APPEND, key, event) < 0) {
        flux_log_error (h, "emit event: flux_kvs_txn_put");
        goto out;
    }
    if (!(f = flux_kvs_commit (h, job->ns, 0, txn)))
        flux_log_error (h, "emit event: flux_kvs_commit");
out:
    saved_errno = errno;
    free (event);
    flux_kvs_txn_destroy (txn);
    errno = saved_errno;
    return f;
}

static flux_future_t * jobinfo_emit_event (struct jobinfo *job,
                                           const char *name,
                                           const char *fmt, ...)
{
    flux_future_t *f = NULL;
    va_list ap;
    va_start (ap, fmt);
    f = jobinfo_emit_eventv (job, name, fmt, ap);
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

/*
 *  Send an event "open loop" -- takes a reference to the job and
 *   releases it in the continuation, logging an error if one was
 *   received.
 */
static int jobinfo_emit_event_nowait (struct jobinfo *job,
                                      const char *name,
                                      const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    flux_future_t *f = jobinfo_emit_eventv (job, name, fmt, ap);
    va_end (ap);
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


static void jobinfo_add_cleanup (struct jobinfo *job, const char *name,
                                 cleanup_task_f fn)
{
    (void) zhashx_insert (job->cleanup, name, (void *) fn);
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

static void jobinfo_complete (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    if (h && job->req) {
        jobinfo_emit_event_nowait (job, "complete",
                                        "status=%d",
                                        job->wait_status);
        if (flux_respond_pack (h, job->req, "{s:I s:s s:{s:i}}",
                                            "id", job->id,
                                            "type", "finish",
                                            "data",
                                            "status", job->wait_status) < 0)
            flux_log_error (h, "jobinfo_complete: flux_respond");
    }
}

static void jobinfo_started (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    if (h && job->req) {
        if (jobinfo_respond (h, job, "start", 0) < 0)
            flux_log_error (h, "jobinfo_started: flux_respond");
    }
}

static void jobinfo_kill (struct jobinfo *job)
{
    flux_watcher_stop (job->timer);
    job->running = 0;
    job->wait_status = 0x9; /* Killed */

    /* XXX: Manually send "finish" event here since our timer_cb won't
     *  fire after we've canceled it. In a real workload a kill request
     *  sent to all ranks would terminate processes that would exit and
     *  report wait status through normal channels.
     */
    jobinfo_complete (job);
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
    jobinfo_emit_event_nowait (job, "exception", msg);
    /* If exception_in_progress set, then no need to respond with another
     *  exception back to job manager. O/w, DO respond to job-manager
     *  and set exception-in-progress.
     */
    if (!job->exception_in_progress) {
        job->exception_in_progress = 1;
        if (jobinfo_respond_error (job, errnum, msg) < 0)
            flux_log_error (h, "jobinfo_fatal_verror: jobinfo_respond_error");
    }
    if (job->running)
        jobinfo_kill (job);
    if (jobinfo_finalize (job) < 0) {
        flux_log_error (h, "jobinfo_fatal_verror: jobinfo_finalize");
        jobinfo_decref (job);
    }
}

static void jobinfo_fatal_error (struct jobinfo *job, int errnum,
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

static double jobspec_duration (flux_t *h, json_t *jobspec)
{
    const char *s;
    double duration = 0.;
    if (json_unpack (jobspec, "{s:{s:{s:s}}}",
                              "attributes", "system",
                              "duration", &s) < 0)
        return -1.;
    if (fsd_parse_duration (s, &duration) < 0) {
        flux_log (h, LOG_ERR, "Unable to parse jobspec duration %s", s);
        return -1.;
    }
    return duration;
}

static int init_testconf (flux_t *h, struct testconf *conf, json_t *jobspec)
{
    const char *tclean = NULL;
    const char *trun = NULL;
    json_t *test = NULL;
    json_error_t err;

    /* get/set defaults */
    conf->run_duration = jobspec_duration (h, jobspec);
    conf->cleanup_duration = -1.;
    conf->wait_status = 0;
    conf->mock_exception = NULL;

    if (json_unpack_ex (jobspec, &err, 0,
                     "{s:{s:{s:{s:o}}}}",
                     "attributes", "system", "exec",
                     "test", &test) < 0)
        return 0;
    if (json_unpack_ex (test, &err, 0,
                        "{s?s s?s s?i s?s}",
                        "run_duration", &trun,
                        "cleanup_duration", &tclean,
                        "wait_status", &conf->wait_status,
                        "mock_exception", &conf->mock_exception) < 0) {
        flux_log (h, LOG_ERR, "init_testconf: %s", err.text);
        return -1;
    }
    if (trun && fsd_parse_duration (trun, &conf->run_duration) < 0)
        flux_log (h, LOG_ERR, "Unable to parse run duration: %s", trun);
    if (tclean && fsd_parse_duration (tclean, &conf->cleanup_duration) < 0)
        flux_log (h, LOG_ERR, "Unable to parse cleanup duration: %s", tclean);
    return 0;
}

/*  Return true if a mock exception was configured for call site "where"
 */
static bool jobinfo_mock_exception (struct jobinfo *job, const char *where)
{
    const char *s = job->testconf.mock_exception;
    return (s && strcmp (where, s) == 0);
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

    if (flux_job_kvs_key (dst, sizeof (dst), true, job->id, "guest") < 0) {
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
static void namespace_move (flux_future_t *fprev, void *arg)
{
    struct jobinfo *job = arg;
    flux_t *h = job->ctx->h;
    flux_future_t *f = NULL;
    flux_future_t *fnext = NULL;

    if (!(f = jobinfo_emit_event (job, "done", NULL))) {
        flux_log_error (h, "namespace_move: jobinfo_emit_event");
        goto error;
    }
    if (   !(fnext = flux_future_and_then (f, namespace_copy, job))
        || !(fnext = flux_future_and_then (f=fnext, namespace_delete, job))) {
        flux_log_error (h, "namespace_move: flux_future_and_then");
        goto error;
    }
    flux_future_continue (fprev, fnext);
    flux_future_destroy (fprev);
    return;
error:
    flux_future_continue_error (fprev, errno, NULL);
    flux_future_destroy (f);
    flux_future_destroy (fnext);
    flux_future_destroy (fprev);
}

/*  Start all cleanup tasks on the cleanup list and return
 *   a composite future that will be ready when everything is done.
 */
static void jobinfo_cleanup (flux_future_t *fprev, void *arg)
{
    struct jobinfo *job = arg;
    flux_t *h = job->ctx->h;
    cleanup_task_f fn;
    flux_future_t *f = NULL;
    flux_future_t *cf = NULL;

    if (!(cf = flux_future_wait_all_create ())) {
        flux_log_error (job->ctx->h, "flux_future_wait_all_create");
        goto error;
    }
    flux_future_set_flux (cf, h);

    fn = zhashx_first (job->cleanup);
    while (fn) {
        const char *name = zhashx_cursor (job->cleanup);
        if (!(f = (*fn) (job))) {
            flux_log_error (h, "%s",
                            (const char *) zhashx_cursor (job->cleanup));
            goto error;
        }
        flux_future_push (cf, name, f);
        fn = zhashx_next (job->cleanup);
    }
    flux_future_continue (fprev, cf);
    flux_future_destroy (fprev);
    return;
error:
    flux_future_continue_error (fprev, errno, NULL);
    flux_future_destroy (fprev);
}

static void emit_cleanup_finish (flux_future_t *prev, void *arg)
{
    struct jobinfo *job = arg;
    flux_future_t *f = NULL;
    int rc = flux_future_get (prev, NULL);

    /*  XXX: It isn't clear what to do if a cleanup task fails.
     *   For now, log the return code from the cleanup composite
     *   future and errno if rc < 0 for informational purposes,
     *   but do not generate an exception.
     */
    if (!(f = jobinfo_emit_event (job, "cleanup.finish",
                                       "rc=%d%s%s", rc,
                                        rc < 0 ? " " : "",
                                        rc < 0 ? strerror (errno) : "")))
        flux_future_continue_error (prev, errno, NULL);
    else
        flux_future_continue (prev, f);
    flux_future_destroy (prev);
}

/*  Start all cleanup tasks:
 *   1. emit cleanup.start event to exec.eventlog
 *   2. start all cleanup work in parallel
 *   3. emit cleanup.done event to exec.eventlog
 *  Returns a chained future that will be fulfilled when these steps
 *   are complete.
 */
static flux_future_t * jobinfo_start_cleanup (struct jobinfo *job)
{
    flux_future_t *f = NULL;
    flux_future_t *fnext = NULL;

    /* Skip cleanup if there are no items on the cleanup list.
     * (e.g. an exception ocurred during job preparation)
     */
    if (zhashx_size (job->cleanup) == 0) {
        /* Return an empty, fulfilled future */
        if (!(f = flux_future_create (NULL, NULL)))
            goto error;
        flux_future_set_flux (f, job->ctx->h);
        flux_future_fulfill (f, NULL, NULL);
        return f;
    }

    /*  O/w, create cleanup composite future sandwiched by
     *   cleanup.start and cleanup.finish events in the eventlog
     */
    if (!(f = jobinfo_emit_event (job, "cleanup.start", NULL)))
        goto error;
    if (!(fnext = flux_future_and_then (f, jobinfo_cleanup, job)))
        goto error;
    f = fnext;
    if (!(fnext = flux_future_and_then (f, emit_cleanup_finish, job)))
        goto error;
    return fnext;
error:
    flux_future_destroy (f);
    flux_future_destroy (fnext);
    return NULL;
}

static void jobinfo_release (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    if (jobinfo_send_release (job, NULL) < 0)
        flux_log_error (job->ctx->h, "jobinfo_send_release");
    /* Should be final destruction */
    jobinfo_decref (job);
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
    flux_future_t *fnext = NULL;

    if (job->finalizing)
        return 0;
    job->finalizing = 1;

    if (!(f = jobinfo_start_cleanup (job)))
        goto error;
    if (job->has_namespace &&
        !(fnext = flux_future_and_then (f, namespace_move, job)))
        goto error;
    if (flux_future_then (fnext, -1., jobinfo_release, job) < 0)
        goto error;
    return 0;
error:
    jobinfo_respond_error (job, errno, "finalize error");
    flux_future_destroy (f);
    flux_future_destroy (fnext);
    return -1;
}

/* Timer callback, post the "finish" event and start "cleanup" tasks.
 */
void timer_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct jobinfo *job = arg;
    job->running = 0;
    job->wait_status = job->testconf.wait_status;
    jobinfo_complete (job);
    if (jobinfo_finalize (job) < 0)
        flux_log_error (job->ctx->h, "jobinfo_finalize");
}

/*  Start a timer to simulate job shell execution. A start event
 *   is sent before the timer is started, and the "finish" event
 *   is sent when the timer fires (simulating the exit of the final
 *   job shell.)
 */
static int jobinfo_start_timer (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    flux_reactor_t *r = flux_get_reactor (h);
    double t = job->testconf.run_duration;

    /*  For now, if a job duration wasn't found, complete job almost
     *   immediately.
     */
    if (t < 0.)
        t = 1.e-5;
    if (t > 0.) {
        job->timer = flux_timer_watcher_create (r, t, 0., timer_cb, job);
        if (!job->timer) {
            flux_log_error (h, "jobinfo_start: timer_create");
            return -1;
        }
        flux_watcher_start (job->timer);
        jobinfo_emit_event_nowait (job, "running", "timer=%.6fs", t);
        job->running = 1;
    }
    else
        return -1;
    return 0;
}

void epilog_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    flux_future_fulfill ((flux_future_t *) arg, NULL, NULL);
    flux_watcher_destroy (w);
}

static flux_future_t * ersatz_epilog (struct jobinfo *job)
{
    flux_t *h = job->ctx->h;
    flux_reactor_t *r = flux_get_reactor (h);
    flux_future_t *f = NULL;
    flux_watcher_t *timer = NULL;
    double t = job->testconf.cleanup_duration;

    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    flux_future_set_flux (f, h);

    if (!(timer = flux_timer_watcher_create (r, t, 0.,
                                             epilog_timer_cb, f))) {
        flux_log_error (h, "flux_timer_watcher_create");
        flux_future_fulfill_error (f, errno, "flux_timer_watcher_create");
    }
    flux_watcher_start (timer);
    return f;
}

static int jobinfo_start_execution (struct jobinfo *job)
{
    jobinfo_emit_event_nowait (job, "starting", NULL);
    if (jobinfo_start_timer (job) < 0) {
        jobinfo_fatal_error (job, errno, "start timer failed");
        return -1;
    }
    jobinfo_started (job);
    if (job->needs_cleanup)
        jobinfo_add_cleanup (job, "epilog simulator", ersatz_epilog);
    return 0;
}

/*  Lookup key 'key' under jobid 'id' kvs dir:
 */
static flux_future_t *flux_jobid_kvs_lookup (flux_t *h, flux_jobid_t id,
                                             int flags, const char *key)
{
    char path [256];
    if (flux_job_kvs_key (path, sizeof (path), true, id, key) < 0)
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
    if (init_testconf (job->ctx->h, &job->testconf, job->jobspec) < 0) {
        jobinfo_fatal_error (job, errno, "failed to initialize testconf");
        goto done;
    }
    if (job->testconf.cleanup_duration > 0.)
        job->needs_cleanup = 1;
    if (jobinfo_mock_exception (job, "init")) {
        jobinfo_fatal_error (job, 0, "mock initialization exception generated");
        goto done;
    }
    if (jobinfo_start_execution (job) < 0) {
        jobinfo_fatal_error (job, errno, "failed to start execution");
        goto done;
    }
    if (jobinfo_mock_exception (job, "run")) {
        jobinfo_fatal_error (job, 0, "mock run exception generated");
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

    if (flux_job_kvs_key (key, sizeof (key), true, job->id, "guest") < 0) {
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
    if (!(f = jobinfo_emit_event (job, "init", NULL))
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

    /* Increase refcount during init phase in case job is canceled:
     */
    jobinfo_incref (job);
    return f;
err:
    flux_log_error (job->ctx->h, "jobinfo_kvs_lookup/namespace_create");
    flux_future_destroy (f);
    return NULL;
}

/*  Create namespace name for jobid 'id'
 */
static int job_get_ns_name (char *buf, int bufsz, flux_jobid_t id)
{
    return fluid_encode (buf, bufsz, id, FLUID_STRING_DOTHEX);
}

static int job_start (struct job_exec_ctx *ctx, const flux_msg_t *msg)
{
    flux_future_t *f = NULL;
    struct jobinfo *job;

    if (!(job = jobinfo_new ()))
        return -1;

    if (!(job->req = flux_msg_copy (msg, true))) {
        flux_log_error (ctx->h, "start: flux_msg_copy");
        jobinfo_decref (job);
        if (flux_respond_error (ctx->h, msg, errno, "flux_msg_copy failed") < 0)
            flux_log_error (ctx->h, "flux_respond_error");
        return -1;
    }
    job->ctx = ctx;

    if (flux_request_unpack (job->req, NULL, "{s:I, s:i}",
                                             "id", &job->id,
                                             "userid", &job->userid) < 0) {
        flux_log_error (ctx->h, "start: flux_request_unpack");
        goto error;
    }
    if (job_get_ns_name (job->ns, sizeof (job->ns), job->id) < 0) {
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
