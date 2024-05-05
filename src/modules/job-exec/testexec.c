/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Flux job test exec implementation
 *
 * DESCRIPTION
 *
 * This exec module implements timer driven test execution without any
 * job-shells for testing and demonstration purposes. The module is
 * activated either when it is the only active exec implementation
 * loaded, or if the exec test configuration block is present in the
 * submitted jobspec. See TEST CONFIGURATION for more information.
 *
 * TEST CONFIGURATION
 *
 *  The job-exec module supports an object in the jobspec under
 * attributes.system.exec.test, which supports the following keys
 *
 * {
 *   "run_duration":s,      - alternate/override attributes.system.duration
 *   "wait_status":i        - report this value as status in the "finish" resp
 *   "mock_exception":s     - mock an exception during this phase of job
 *                             execution (currently "init" and "run")
 *   "override":i           - exec override mode: wait for RPC to emit start
 *                             event a testexec job. If job has unlimited
 *                             duration, then also wait for finish RPC or
 *                             job exception for job finish event.
 *   "reattach_finish":i    - if reattached, assume job has finished
 *                             and no longer has remaining time to run.
 *                             Useful for testing job reattach.
 * }
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <time.h>
#include <sys/time.h>

#include "src/common/libutil/fsd.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "job-exec.h"

struct testexec_ctx {
    flux_t *h;
    flux_msg_handler_t *mh;  /* testexec RPC handler */
    flux_msg_handler_t *dh;  /* disconnect handler */
    zhashx_t *jobs;
};

struct testconf {
    bool                  enabled;          /* test execution enabled       */
    int                   override;         /* wait for RPC for start event */
    int                   reattach_finish;  /* if reattached, just finish job */
    double                run_duration;     /* duration of fake job in sec  */
    int                   wait_status;      /* reported status for "finish" */
    const char *          mock_exception;   /* fake exception at this site  */
};

struct testexec {
    struct jobinfo *job;
    struct testconf conf;
    struct idset *ranks;
    flux_watcher_t *timer;
};


static struct testexec_ctx *testexec_ctx = NULL;


static struct testexec * testexec_create (struct jobinfo *job,
                                          struct testconf conf)
{
    struct testexec *te = calloc (1, sizeof (*te));
    if (te == NULL)
        return NULL;
    te->job = job;
    te->conf = conf;
    return (te);
}

static void testexec_destroy (struct testexec *te)
{
    flux_watcher_destroy (te->timer);
    idset_destroy (te->ranks);
    free (te);
}

static double jobspec_duration (flux_t *h, json_t *jobspec)
{
    double duration = 0.;
    if (json_unpack (jobspec, "{s:{s:{s:F}}}",
                              "attributes", "system",
                              "duration", &duration) < 0)
        return -1.;
    return duration;
}

static int init_testconf (flux_t *h, struct testconf *conf, json_t *jobspec)
{
    const char *trun = NULL;
    json_t *test = NULL;
    json_error_t err;

    /* get/set defaults */
    conf->run_duration = jobspec_duration (h, jobspec);
    conf->override = 0;
    conf->reattach_finish = 0;
    conf->wait_status = 0;
    conf->mock_exception = NULL;
    conf->enabled = false;

    if (json_unpack_ex (jobspec,
                        &err,
                        0,
                        "{s:{s:{s:{s:o}}}}",
                        "attributes",
                         "system",
                          "exec",
                           "test", &test) < 0)
        return 0;
    conf->enabled = true;
    if (json_unpack_ex (test,
                        &err, 0,
                        "{s?s s?i s?i s?i s?s}",
                        "run_duration", &trun,
                        "override", &conf->override,
                        "reattach_finish", &conf->reattach_finish,
                        "wait_status", &conf->wait_status,
                        "mock_exception", &conf->mock_exception) < 0) {
        flux_log (h, LOG_ERR, "init_testconf: %s", err.text);
        return -1;
    }
    if (trun && fsd_parse_duration (trun, &conf->run_duration) < 0)
        flux_log (h, LOG_ERR, "Unable to parse run duration: %s", trun);
    return 0;
}

/*  Return true if mock exception was configured for call site "where"
 */
static bool testconf_mock_exception (struct testconf *conf, const char *where)
{
    const char *s = conf->mock_exception;
    return (s && streq (s, where));
}

/* Timer callback, post the "finish" event and notify tasks are complete
 */
static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct testexec *te = arg;

    /* Notify job-exec that tasks have completed:
     */
    jobinfo_tasks_complete (te->job,
                            resource_set_ranks (te->job->R),
                            te->conf.wait_status);
}

/*  Start a timer to simulate job shell execution. A start event
 *   is sent before the timer is started, and the "finish" event
 *   is sent when the timer fires (simulating the exit of the final
 *   job shell.)
 */
static int start_timer (flux_t *h, struct testexec *te, double t)
{
    flux_reactor_t *r = flux_get_reactor (h);

    /*  For now, if a job duration wasn't found, complete job almost
     *   immediately.
     */
    if (t < 0.)
        t = 1.e-5;
    if (t >= 0.) {
        if (t > 0.) {
            te->timer = flux_timer_watcher_create (r, t, 0., timer_cb, te);
            if (!te->timer) {
                flux_log_error (h, "jobinfo_start: timer_create");
                return -1;
            }
            flux_watcher_start (te->timer);
        }
        if (te->job->reattach)
            jobinfo_reattached (te->job);
        else
            jobinfo_started (te->job);
    }
    else
        return -1;
    return 0;
}

static int testexec_init (struct jobinfo *job)
{
    flux_t *h = job->h;
    struct testexec *te = NULL;
    struct testconf conf;

    if (init_testconf (h, &conf, job->jobspec) < 0) {
        jobinfo_fatal_error (job, errno, "failed to initialize testconf");
        return -1;
    }
    else if (!conf.enabled)
        return 0;
    if (!(te = testexec_create (job, conf))) {
        jobinfo_fatal_error (job, errno, "failed to init test exec module");
        return -1;
    }
    job->data = (void *) te;
    if (testconf_mock_exception (&te->conf, "init")) {
        jobinfo_fatal_error (job, 0, "mock initialization exception generated");
        testexec_destroy (te);
        return -1;
    }
    if (zhashx_insert (testexec_ctx->jobs, &job->id, te) < 0) {
        jobinfo_fatal_error (job, 0, "testexec: zhashx_insert failed");
        testexec_destroy (te);
        return -1;
    }
    return 1;
}

static int testexec_reattach_starttime (struct jobinfo *job,
                                        const char *eventlog,
                                        time_t *startp)
{
    json_t *o = NULL;
    size_t index;
    json_t *value;
    int rv = -1;

    if (!(o = eventlog_decode (eventlog))) {
        jobinfo_fatal_error (job, errno, "eventlog_decode");
        goto cleanup;
    }

    json_array_foreach (o, index, value) {
        double timestamp;
        const char *name;

        if (eventlog_entry_parse (value, &timestamp, &name, NULL) < 0) {
            jobinfo_fatal_error (job, errno, "eventlog_entry_parse");
            goto cleanup;
        }
        if (streq (name, "start")) {
            (*startp) = (time_t)timestamp;
            break;
        }
    }

    rv = 0;
cleanup:
    json_decref (o);
    return rv;
}

static int testexec_reattach (struct testexec *te)
{
    const char *value;
    flux_future_t *f = NULL;
    time_t start = 0;
    struct timespec now;
    time_t runtimeleft = -1;
    int rv = -1;
    char ekey[256];

    if (flux_job_kvs_key (ekey, sizeof (ekey), te->job->id, "eventlog") < 0) {
        jobinfo_fatal_error (te->job, errno, "flux_job_kvs_key");
        goto cleanup;
    }
    if (!(f = flux_kvs_lookup (te->job->h,
                               NULL,
                               0,
                               ekey))) {
        jobinfo_fatal_error (te->job, errno, "flux_kvs_lookup");
        goto cleanup;
    }
    if (flux_kvs_lookup_get (f, &value) < 0) {
        jobinfo_fatal_error (te->job, errno, "flux_kvs_lookup_get starttimes");
        goto cleanup;
    }
    if (testexec_reattach_starttime (te->job, value, &start) < 0)
        goto cleanup;
    if (!te->conf.reattach_finish) {
        /* just use seconds, we approximate runtime left */
        clock_gettime (CLOCK_REALTIME, &now);
        if ((now.tv_sec - start) <= te->conf.run_duration)
            runtimeleft = (start + te->conf.run_duration) - now.tv_sec;
    }
    if (start_timer (te->job->h,
                     te,
                     runtimeleft) < 0) {
        jobinfo_fatal_error (te->job, errno, "unable to restart timer");
        goto cleanup;
    }
    rv = 0;
cleanup:
    flux_future_destroy (f);
    return rv;
}

static int testexec_start (struct jobinfo *job)
{
    struct testexec *te = job->data;

    if (job->reattach) {
        if (testexec_reattach (te) < 0)
            return -1;
    }
    else {
        if (!te->conf.override && start_timer (job->h,
                                               te,
                                               te->conf.run_duration) < 0) {
            jobinfo_fatal_error (job, errno, "unable to start test exec timer");
            return -1;
        }
        if (testconf_mock_exception (&te->conf, "run")) {
            jobinfo_fatal_error (job, 0, "mock run exception generated");
            return -1;
        }
    }
    return 0;
}

static int testexec_kill (struct jobinfo *job, int signum)
{
    struct testexec *te = job->data;

    flux_watcher_stop (te->timer);

    /* XXX: Manually send "finish" event here since our timer_cb won't
     *  fire after we've canceled it. In a real workload a kill request
     *  sent to all ranks would terminate processes that would exit and
     *  report wait status through normal channels.
     */
    if (job->started)
        jobinfo_tasks_complete (job, te->ranks, signum);
    return 0;
}

static void testexec_exit (struct jobinfo *job)
{
    zhashx_delete (testexec_ctx->jobs, &job->id);
    job->data = NULL;
}

static void testexec_request_cb (flux_t *h,
                                 flux_msg_handler_t *mh,
                                 const flux_msg_t *msg,
                                 void *arg)
{
    const char *errmsg = NULL;
    struct testexec_ctx *ctx = arg;
    struct testexec *te;
    const char *event;
    flux_jobid_t id;
    uint32_t owner;
    int code = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:I s?i}",
                             "event", &event,
                             "jobid", &id,
                             "status", &code) < 0)
        goto error;
    if (!(te = zhashx_lookup (ctx->jobs, &id))) {
        errmsg = "Job not found";
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_get_userid (msg, &owner) < 0
        || owner != te->job->userid) {
        errmsg = "Permission denied";
        errno = EPERM;
        goto error;
    }
    if (!te->conf.override) {
        errmsg = "Job not in exec override mode";
        errno = EINVAL;
        goto error;
    }
    if (streq (event, "start")) {
        if (te->job->running) {
            errmsg = "Job already running";
            errno = EINVAL;
            goto error;
        }
        if (start_timer (h, te, te->conf.run_duration) < 0)
            goto error;
    }
    else if (streq (event, "finish")) {
        if (!te->job->running) {
            errmsg = "Job not running";
            errno = EINVAL;
            goto error;
        }
        flux_watcher_stop (te->timer);
        jobinfo_tasks_complete (te->job, te->ranks, code);
    }
    else {
        errmsg = "Invalid event";
        errno = EINVAL;
        goto error;
    }

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);

    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void testexec_ctx_destroy (struct testexec_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_destroy (ctx->mh);
        flux_msg_handler_destroy (ctx->dh);
        zhashx_destroy (&ctx->jobs);
        free (ctx);
        errno = saved_errno;
    }
}

static void testexec_destructor (void **item)
{
    if (item) {
        struct testexec *te = *item;
        testexec_destroy (te);
        *item = NULL;
    }
}

static struct testexec_ctx *testexec_ctx_create (flux_t *h)
{
    struct flux_match match = FLUX_MATCH_REQUEST;
    struct testexec_ctx *ctx = calloc (1, sizeof (*ctx));
    if (!ctx)
        return NULL;
    ctx->h = h;

    match.topic_glob = "job-exec.override";
    ctx->mh = flux_msg_handler_create (h,
                                       match,
                                       testexec_request_cb,
                                       ctx);
    if (!ctx->mh
        || !(ctx->jobs = job_hash_create ()))
        goto error;
    zhashx_set_destructor (ctx->jobs, testexec_destructor);

    flux_msg_handler_allow_rolemask (ctx->mh, FLUX_ROLE_USER);
    flux_msg_handler_start (ctx->mh);

    return ctx;

error:
    testexec_ctx_destroy (ctx);
    return NULL;
}

static int testexec_config (flux_t *h, int argc, char **argv)
{
    if (!(testexec_ctx = testexec_ctx_create (h)))
        return -1;
    return 0;
}

static void testexec_unload (void)
{
    testexec_ctx_destroy (testexec_ctx);
    // Ensure a reload will not re-use a freed value
    testexec_ctx = NULL;
}

struct exec_implementation testexec = {
    .name =     "testexec",
    .config =   testexec_config,
    .unload =   testexec_unload,
    .init =     testexec_init,
    .exit =     testexec_exit,
    .start =    testexec_start,
    .kill =     testexec_kill,
    .stats =    NULL,
};

/* vi: ts=4 sw=4 expandtab
 */
