/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* Simple scheduler for testing:
 * - presume that each job is requesting one core
 * - track core counts, not specific core id's
 * - mode=single
 *
 * Command line usage:
 *   flux module load sched-dummy [--cores=N]
 * Options
 *   --cores=N      specifies the total number of cores available (default 16)
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>
#include <czmq.h>
#include "src/common/liboptparse/optparse.h"
#include "src/common/libschedutil/schedutil.h"

// flux module debug --setbit 0x1 sched-dummy
// flux module debug --clearbit 0x1 sched-dummy
enum module_debug_flags {
    DEBUG_FAIL_ALLOC = 1,  // while set, alloc requests fail
};

struct job {
    flux_msg_t *msg;
    flux_jobid_t id;
    int priority;
    uint32_t userid;
    double t_submit;
    char *jobspec;
};

struct sched_ctx {
    flux_t *h;
    struct ops_context *sched_ops;
    optparse_t *opt;
    struct job *job;  // backlog of 1 alloc request
    int cores_total;
    int cores_free;
    flux_watcher_t *prep;
};

static void job_destroy (struct job *job)
{
    if (job) {
        int saved_errno = errno;
        free (job->jobspec);
        flux_msg_destroy (job->msg);
        free (job);
        errno = saved_errno;
    }
}

/* Create job struct from sched.alloc request.
 */
static struct job *job_create (const flux_msg_t *msg, const char *jobspec)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    if (schedutil_alloc_request_decode (msg,
                                        &job->id,
                                        &job->priority,
                                        &job->userid,
                                        &job->t_submit)
        < 0)
        goto error;
    if (!(job->jobspec = strdup (jobspec)))
        goto error;
    if (!(job->msg = flux_msg_copy (msg, true)))
        goto error;
    return job;
error:
    job_destroy (job);
    return NULL;
}

static bool test_debug_flag (flux_t *h, int mask)
{
    int *debug_flags = flux_aux_get (h, "flux::debug_flags");

    if (debug_flags && (*debug_flags & mask))
        return true;
    return false;
}

void try_alloc (struct sched_ctx *sc)
{
    if (sc->job) {
        if (test_debug_flag (sc->h, DEBUG_FAIL_ALLOC)) {
            if (schedutil_alloc_respond_denied (sc->h, sc->job->msg, "DEBUG_FAIL_ALLOC")
                < 0)
                flux_log_error (sc->h, "schedutil_alloc_respond_denied");
            goto done;
        }
        if (sc->cores_free > 0) {
            if (schedutil_alloc_respond_R (sc->h, sc->job->msg, "1core", NULL) < 0)
                flux_log_error (sc->h, "schedutil_alloc_respond_R");
            sc->cores_free--;
            goto done;
        }
        if (schedutil_alloc_respond_note (sc->h, sc->job->msg, "no cores available")
            < 0)
            flux_log_error (sc->h, "schedutil_alloc_respond_note");
    }
    return;
done:
    job_destroy (sc->job);
    sc->job = NULL;
}

void exception_cb (flux_t *h,
                   flux_jobid_t id,
                   const char *type,
                   int severity,
                   void *arg)
{
    struct sched_ctx *sc = arg;
    char note[80];

    if (severity > 0 || sc->job == NULL || sc->job->id != id)
        return;
    (void)
        snprintf (note, sizeof (note), "alloc aborted due to exception type=%s", type);
    if (schedutil_alloc_respond_denied (h, sc->job->msg, note) < 0)
        flux_log_error (h, "%s: alloc_respond_denied", __FUNCTION__);
    job_destroy (sc->job);
    sc->job = NULL;
}

void free_cb (flux_t *h, const flux_msg_t *msg, const char *R, void *arg)
{
    struct sched_ctx *sc = arg;
    flux_jobid_t id;

    if (schedutil_free_request_decode (msg, &id) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "free: id=%llu R=%s", (unsigned long long)id, R);
    sc->cores_free++;
    if (schedutil_free_respond (h, msg) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    try_alloc (sc);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void alloc_cb (flux_t *h, const flux_msg_t *msg, const char *jobspec, void *arg)
{
    struct sched_ctx *sc = arg;
    struct job *job;

    if (!(job = job_create (msg, jobspec))) {
        flux_log_error (h, "%s: job_create", __FUNCTION__);
        goto error;
    }
    if (sc->job) {
        flux_log_error (h, "alloc received before previous one handled");
        goto error;
    }
    sc->job = job;
    flux_log (h,
              LOG_DEBUG,
              "alloc: id=%llu jobspec=%s",
              (unsigned long long)job->id,
              job->jobspec);
    try_alloc (sc);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

int hello_cb (flux_t *h, const char *R, void *arg)
{
    struct sched_ctx *sc = arg;

    flux_log (h, LOG_DEBUG, "%s: R=%s", __FUNCTION__, R);
    sc->cores_free--;
    return 0;
}

static struct optparse_option dummy_opts[] = {
    {
        .name = "cores",
        .has_arg = 1,
        .flags = 0,
        .arginfo = "COUNT",
        .usage = "Core count (default 16)",
    },
    OPTPARSE_TABLE_END,
};

/* N.B. module argv[0] is first argument, not module name.
 */
optparse_t *options_parse (int argc, char **argv)
{
    optparse_t *opt;
    if (!(opt = optparse_create ("sched-dummy"))) {
        errno = ENOMEM;
        return NULL;
    }
    if (optparse_add_option_table (opt, dummy_opts) != OPTPARSE_SUCCESS)
        goto error;
    if (optparse_parse_args (opt, argc + 1, argv - 1) < 0)
        goto error;
    return opt;
error:
    optparse_destroy (opt);
    errno = EINVAL;
    return NULL;
}

void sched_destroy (struct sched_ctx *sc)
{
    if (sc) {
        int saved_errno = errno;
        schedutil_ops_unregister (sc->sched_ops);
        optparse_destroy (sc->opt);
        if (sc->job) {
            /* Causes job-manager to pause scheduler interface.
             */
            if (flux_respond_error (sc->h, sc->job->msg, ENOSYS, "scheduler unloading")
                < 0)
                flux_log_error (sc->h, "flux_respond_error");
            job_destroy (sc->job);
        }
        free (sc);
        errno = saved_errno;
    }
}

struct sched_ctx *sched_create (flux_t *h, int argc, char **argv)
{
    struct sched_ctx *sc;

    if (!(sc = calloc (1, sizeof (*sc))))
        return NULL;
    sc->h = h;
    if (!(sc->sched_ops =
              schedutil_ops_register (h, alloc_cb, free_cb, exception_cb, sc))) {
        flux_log_error (h, "schedutil_ops_register");
        goto error;
    }
    if (!(sc->opt = options_parse (argc, argv))) {
        errno = EINVAL;
        goto error;
    }
    sc->cores_total = optparse_get_int (sc->opt, "cores", 16);
    sc->cores_free = sc->cores_total;
    return sc;
error:
    sched_destroy (sc);
    return NULL;
}

int mod_main (flux_t *h, int argc, char *argv[])
{
    int rc = -1;
    struct sched_ctx *sc;
    int count;

    if (!(sc = sched_create (h, argc, argv)))
        return -1;
    flux_log (h, LOG_DEBUG, "res pool is %d cores", sc->cores_total);
    if (schedutil_hello (h, hello_cb, sc) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto done;
    }
    if (schedutil_ready (h, "single", &count) < 0) {
        flux_log_error (h, "schedutil_ready");
        goto done;
    }
    flux_log (sc->h, LOG_DEBUG, "ready: count=%d", count);

    if ((rc = flux_reactor_run (flux_get_reactor (h), 0)) < 0)
        flux_log_error (h, "flux_reactor_run");
done:
    sched_destroy (sc);
    return rc;
}
MOD_NAME ("sched-dummy");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
