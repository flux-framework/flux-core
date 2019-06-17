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
 * This exec module implments timer driven test execution without any
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
 *   "cleanup_duration":s   - enable a fake job epilog and set its duration
 *   "wait_status":i        - report this value as status in the "finish" resp
 *   "mock_exception":s     - mock an exception during this phase of job
 *                             execution (currently "init" and "run")
 * }
 *
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "src/common/libutil/fsd.h"
#include "job-exec.h"

struct testconf {
    bool                  enabled;          /* test execution enabled       */
    double                run_duration;     /* duration of fake job in sec  */
    double                cleanup_duration; /* if > 0., duration of epilog  */
    int                   wait_status;      /* reported status for "finish" */
    const char *          mock_exception;   /* fake excetion at this site   */
};

struct testexec {
    struct testconf conf;
    struct idset *ranks;
    flux_watcher_t *timer;
};

static struct testexec * testexec_create (struct testconf conf)
{
    struct testexec *te = calloc (1, sizeof (*te));
    if (te == NULL)
        return NULL;
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
    const char *tclean = NULL;
    const char *trun = NULL;
    json_t *test = NULL;
    json_error_t err;

    /* get/set defaults */
    conf->run_duration = jobspec_duration (h, jobspec);
    conf->cleanup_duration = -1.;
    conf->wait_status = 0;
    conf->mock_exception = NULL;
    conf->enabled = false;

    if (json_unpack_ex (jobspec, &err, 0,
                     "{s:{s:{s:{s:o}}}}",
                     "attributes", "system", "exec",
                     "test", &test) < 0)
        return 0;
    conf->enabled = true;
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

/*  Return true if mock exception was configured for call site "where"
 */
static bool testconf_mock_exception (struct testconf *conf, const char *where)
{
    const char *s = conf->mock_exception;
    return (s && strcmp (s, where) == 0);
}

/* Timer callback, post the "finish" event and start "cleanup" tasks.
 */
static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents, void *arg)
{
    struct jobinfo *job = arg;
    struct testexec *te = job->data;

    /* Notify job-exec that tasks have completed:
     */
    jobinfo_tasks_complete (job,
                            resource_set_ranks (job->R),
                            te->conf.wait_status);
}

/*  Start a timer to simulate job shell execution. A start event
 *   is sent before the timer is started, and the "finish" event
 *   is sent when the timer fires (simulating the exit of the final
 *   job shell.)
 */
static int start_timer (flux_t *h, struct testexec *te, struct jobinfo *job)
{
    flux_reactor_t *r = flux_get_reactor (h);
    double t = te->conf.run_duration;

    /*  For now, if a job duration wasn't found, complete job almost
     *   immediately.
     */
    if (t < 0.)
        t = 1.e-5;
    if (t > 0.) {
        char timebuf[256];
        te->timer = flux_timer_watcher_create (r, t, 0., timer_cb, job);
        if (!te->timer) {
            flux_log_error (h, "jobinfo_start: timer_create");
            return -1;
        }
        flux_watcher_start (te->timer);
        snprintf (timebuf, sizeof (timebuf), "%.6fs", t);
        jobinfo_started (job, "{ s:s }", "timer", timebuf);
    }
    else
        return -1;
    return 0;
}


static void cleanup_timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                             int revents, void *arg)
{
    flux_future_fulfill ((flux_future_t *) arg, NULL, NULL);
    flux_watcher_destroy (w);
}

static flux_future_t * fake_cleanup (struct jobinfo *job)
{
    struct testexec *te = job->data;
    flux_t *h = job->h;
    flux_reactor_t *r = flux_get_reactor (h);
    flux_future_t *f = NULL;
    flux_watcher_t *timer = NULL;
    double t = te->conf.cleanup_duration;

    if (!(f = flux_future_create (NULL, NULL)))
        return NULL;
    flux_future_set_flux (f, h);

    /* If no cleanup, immediately fulfill future */
    if (t <= 0.) {
        flux_future_fulfill (f, NULL, NULL);
        return f;
    }

    /*  O/w, start timer to simulate cleanup */
    if (!(timer = flux_timer_watcher_create (r, t, 0.,
                                             cleanup_timer_cb, f))) {
        flux_log_error (h, "flux_timer_watcher_create");
        flux_future_fulfill_error (f, errno, "flux_timer_watcher_create");
    }
    flux_watcher_start (timer);
    return f;
}

static void fake_cleanup_cb (flux_future_t *f, void *arg)
{
    struct jobinfo *job = arg;
    struct idset *idset = flux_future_aux_get (f, "idset");
    /*  XXX: ranks idset not fully supported and may be NULL */
    jobinfo_cleanup_complete (job, idset, 0);
    flux_future_destroy (f);
}

static int fake_cleanup_start (struct jobinfo *job, const struct idset *ranks)
{
    struct idset *idset = ranks ? idset_copy (ranks) : NULL;
    flux_future_t *f = fake_cleanup (job);
    if (!f || flux_future_then (f, -1., fake_cleanup_cb, job) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_aux_set (f, "idset", idset, (flux_free_f) idset_destroy);
    return (0);
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
    // XXX: testexec always enabled for now
    //else if (!conf.enabled)
    //    return 0;
    if (!(te = testexec_create (conf))) {
        jobinfo_fatal_error (job, errno, "failed to init test exec module");
        return -1;
    }
    job->data = (void *) te;
    if (testconf_mock_exception (&te->conf, "init")) {
        jobinfo_fatal_error (job, 0, "mock initialization exception generated");
        testexec_destroy (te);
        return -1;
    }
    return 1;
}

static int testexec_start (struct jobinfo *job)
{
    struct testexec *te = job->data;

    if (start_timer (job->h, te, job) < 0) {
        jobinfo_fatal_error (job, errno, "unable to start test exec timer");
        return -1;
    }
    if (testconf_mock_exception (&te->conf, "run")) {
        jobinfo_fatal_error (job, 0, "mock run exception generated");
        return -1;
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
    jobinfo_tasks_complete (job, te->ranks, signum);
    return 0;
}

static int testexec_cleanup (struct jobinfo *job, const struct idset *idset)
{
    return fake_cleanup_start (job, idset);
}

static void testexec_exit (struct jobinfo *job)
{
    struct testexec *te = job->data;
    testexec_destroy (te);
    job->data = NULL;
}

struct exec_implementation testexec = {
    .name =     "testexec",
    .init =     testexec_init,
    .exit =     testexec_exit,
    .start =    testexec_start,
    .kill =     testexec_kill,
    .cleanup =  testexec_cleanup
};

/* vi: ts=4 sw=4 expandtab
 */
