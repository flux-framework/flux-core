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
#include <flux/schedutil.h>
#include <jansson.h>
#include <czmq.h>
#include "src/common/liboptparse/optparse.h"

// flux module debug --setbit 0x1 sched-dummy
// flux module debug --clearbit 0x1 sched-dummy
enum module_debug_flags {
    DEBUG_FAIL_ALLOC = 1, // while set, alloc requests fail
};

struct job {
    flux_msg_t *msg;
    flux_jobid_t id;
    int priority;
    uint32_t userid;
    double t_submit;
    char *jobspec;
    bool scheduled;
    int annotate_count;
    void *handle;               /* zlistx handle */
};

struct sched_ctx {
    flux_t *h;
    schedutil_t *schedutil_ctx;
    optparse_t *opt;
    int cores_total;
    int cores_free;
    const char *mode;
    flux_watcher_t *prep;
    zlistx_t *jobs;
    flux_msg_handler_t **handlers;
};

static void job_destroy (void *data)
{
    struct job *job = data;
    if (job) {
        int saved_errno = errno;
        free (job->jobspec);
        flux_msg_destroy (job->msg);
        free (job);
        errno = saved_errno;
    }
}

/* for zlistx_set_destructor() */
static void job_destructor (void **data)
{
    if (data)
        job_destroy (*data);
}

/* Taken from modules/job-manager/job.c */

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

static int job_cmp (const void *x, const void *y)
{
    const struct job *j1 = x;
    const struct job *j2 = y;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

static struct job *
job_find (struct sched_ctx *sc, flux_jobid_t id)
{
    struct job *job;
    job = zlistx_first (sc->jobs);
    while (job) {
        if (job->id == id)
            return job;
        job = zlistx_next (sc->jobs);
    }
    return NULL;
}

/* Create job struct from sched.alloc request.
 */
static struct job *job_create (const flux_msg_t *msg, const char *jobspec)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    if (schedutil_alloc_request_decode (msg, &job->id, &job->priority,
                                        &job->userid, &job->t_submit) < 0)
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

static void respond_success_single (struct sched_ctx *sc,
                                    struct job *job)
{
    if (schedutil_alloc_respond_success_pack (sc->schedutil_ctx,
                                              job->msg,
                                              "1core",
                                              "{ s:{s:s s:n} }",
                                              "sched",
                                              "resource_summary", "1core",
                                              "reason_pending") < 0)
        flux_log_error (sc->h, "schedutil_alloc_respond_success_pack");
}

static void respond_success_unlimited (struct sched_ctx *sc,
                                       struct job *job)
{
    if (schedutil_alloc_respond_success_pack (sc->schedutil_ctx,
                                              job->msg,
                                              "1core",
                                              "{ s:{s:s s:n s:n} }",
                                              "sched",
                                              "resource_summary", "1core",
                                              "reason_pending",
                                              "jobs_ahead") < 0)
        flux_log_error (sc->h, "schedutil_alloc_respond_success_pack");
}

static void respond_annotate_single (struct sched_ctx *sc,
                                     struct job *job)
{
    if (schedutil_alloc_respond_annotate_pack (sc->schedutil_ctx,
                                               job->msg,
                                               "{ s:{s:s} }",
                                               "sched", "reason_pending",
                                               "insufficient resources") < 0)
        flux_log_error (sc->h, "schedutil_alloc_respond_annotate_pack");
}

static void respond_annotate_unlimited (struct sched_ctx *sc,
                                        struct job *job,
                                        int jobs_ahead_count)
{
    if (job->annotate_count) {
        if (schedutil_alloc_respond_annotate_pack (sc->schedutil_ctx,
                                                   job->msg,
                                                   "{ s:{s:i} }",
                                                   "sched", "jobs_ahead",
                                                   jobs_ahead_count) < 0)
            flux_log_error (sc->h, "schedutil_alloc_respond_annotate_pack");
    }
    else {
        if (schedutil_alloc_respond_annotate_pack (sc->schedutil_ctx,
                                                   job->msg,
                                                   "{ s:{s:s s:i} }",
                                                   "sched",
                                                   "reason_pending",
                                                   "insufficient resources",
                                                   "jobs_ahead",
                                                   jobs_ahead_count) < 0)
            flux_log_error (sc->h, "schedutil_alloc_respond_annotate_pack");
    }
}

void try_alloc (struct sched_ctx *sc)
{
    struct job *job = zlistx_first (sc->jobs);
    int jobs_ahead_count = 0;

    while (job) {
        if (!job->scheduled) {
            if (flux_module_debug_test (sc->h, DEBUG_FAIL_ALLOC, false)) {
                if (schedutil_alloc_respond_deny (sc->schedutil_ctx,
                                                  job->msg,
                                                  "DEBUG_FAIL_ALLOC") < 0)
                    flux_log_error (sc->h, "schedutil_alloc_respond_deny");
            }
            else if (sc->cores_free > 0) {
                if (!strcmp (sc->mode, "single"))
                    respond_success_single (sc, job);
                else
                    respond_success_unlimited (sc, job);
                job->scheduled = true;
                sc->cores_free--;
            }
            else {
                if (!strcmp (sc->mode, "single"))
                    respond_annotate_single (sc, job);
                else
                    respond_annotate_unlimited (sc, job, jobs_ahead_count);
                job->annotate_count++;
                jobs_ahead_count++;
            }
        }
        /* if in single mode, break after one iteration */
        if (!strcmp (sc->mode, "single"))
            break;

        job = zlistx_next (sc->jobs);
    }

    /* hackish, but safe way to remove jobs safely from list while
     * iterating them */
    job = zlistx_first (sc->jobs);
    while (job) {
        if (job->scheduled) {
            zlistx_delete (sc->jobs, job->handle);
            job = zlistx_first (sc->jobs);
        }
        else
            job = zlistx_next (sc->jobs);
    }
    return;
}

void prioritize_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    struct sched_ctx *sc = arg;
    json_t *jobs;
    size_t index;
    json_t *arr;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0)
        goto proto_error;

    json_array_foreach (jobs, index, arr) {
        flux_jobid_t id;
        int64_t priority;
        struct job *job;

        if (json_unpack (arr, "[I,I]", &id, &priority) < 0)
            goto proto_error;

        if ((job = job_find (sc, id))) {
            job->priority = priority;
            zlistx_reorder (sc->jobs, job->handle, true);
            break;
        }
    }

    /* called to regenerate annotations */
    try_alloc (sc);
    return;

proto_error:
    flux_log (h, LOG_ERR, "malformed sched.reprioritize request");
    return;
}

void cancel_cb (flux_t *h, flux_jobid_t id, void *arg)
{
    struct sched_ctx *sc = arg;
    struct job *job;

    if (!(job = zlistx_first (sc->jobs)))
        return;

    if (!strcmp (sc->mode, "single") && job->id != id)
        return;

    if ((job = job_find (sc, id))) {
        if (schedutil_alloc_respond_cancel (sc->schedutil_ctx,
                                            job->msg) < 0)
            flux_log_error (h, "%s: alloc_respond_cancel", __FUNCTION__);
        zlistx_delete (sc->jobs, job->handle);
    }

    /* called to regenerate annotations */
    try_alloc (sc);
}

void free_cb (flux_t *h, const flux_msg_t *msg, const char *R, void *arg)
{
    struct sched_ctx *sc = arg;
    flux_jobid_t id;

    if (schedutil_free_request_decode (msg, &id) < 0)
        goto error;
    flux_log (h, LOG_DEBUG, "free: id=%ju R=%s",
              (uintmax_t)id, R);
    sc->cores_free++;
    if (schedutil_free_respond (sc->schedutil_ctx, msg) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    try_alloc (sc);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void alloc_cb (flux_t *h, const flux_msg_t *msg,
               const char *jobspec, void *arg)
{
    struct sched_ctx *sc = arg;
    struct job *job;

    if (!(job = job_create (msg, jobspec))) {
        flux_log_error (h, "%s: job_create", __FUNCTION__);
        goto error;
    }
    if (!strcmp (sc->mode, "single") && zlistx_size (sc->jobs) > 0) {
        flux_log_error (h, "alloc received before previous one handled");
        goto error;
    }
    if (!(job->handle = zlistx_insert (sc->jobs, job, true))) {
        flux_log_error (h, "%s: zlistx_insert", __FUNCTION__);
        goto error;
    }
    flux_log (h, LOG_DEBUG, "alloc: id=%ju jobspec=%s",
              (uintmax_t)job->id, job->jobspec);
    try_alloc (sc);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

int hello_cb (flux_t *h,
              flux_jobid_t id,
              int priority,
              uint32_t userid,
              double t_submit,
              const char *R,
              void *arg)
{
    struct sched_ctx *sc = arg;

    flux_log (h, LOG_DEBUG,
              "%s: id=%ju priority=%d userid=%u t_submit=%0.1f R=%s",
              __func__,
              (uintmax_t)id,
              priority,
              (unsigned int)userid,
              t_submit,
              R);
    sc->cores_free--;
    return 0;
}

static struct optparse_option dummy_opts[] = {
    {   .name = "cores",
        .has_arg = 1,
        .flags = 0,
        .arginfo = "COUNT",
        .usage = "Core count (default 16)",
    },
    {   .name = "mode",
        .has_arg = 1,
        .flags = 0,
        .arginfo = "single|unlimited",
        .usage = "Specify mode",
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
    if (optparse_parse_args (opt,  argc + 1, argv - 1) < 0)
        goto error;
    return opt;
error:
    optparse_destroy (opt);
    errno = EINVAL;
    return NULL;
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "sched.prioritize", prioritize_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

void sched_destroy (struct sched_ctx *sc)
{
    if (sc) {
        struct job *job;
        int saved_errno = errno;
        schedutil_destroy (sc->schedutil_ctx);
        optparse_destroy (sc->opt);
        job = zlistx_first (sc->jobs);
        while (job) {
            /* Causes job-manager to pause scheduler interface.
             */
            if (flux_respond_error (sc->h, job->msg, ENOSYS,
                                    "scheduler unloading") < 0)
                flux_log_error (sc->h, "flux_respond_error");
            job = zlistx_next (sc->jobs);
        }
        zlistx_destroy (&sc->jobs);
        flux_msg_handler_delvec (sc->handlers);
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
    sc->schedutil_ctx = schedutil_create (h,
                                          alloc_cb,
                                          free_cb,
                                          cancel_cb,
                                          sc);
    if (sc->schedutil_ctx == NULL) {
        flux_log_error (h, "schedutil_create");
        goto error;
    }
    if (!(sc->opt = options_parse (argc, argv))) {
        errno = EINVAL;
        goto error;
    }
    sc->cores_total = optparse_get_int (sc->opt, "cores", 16);
    sc->cores_free = sc->cores_total;
    sc->mode = optparse_get_str (sc->opt, "mode", "single");
    if (strcmp (sc->mode, "single") && strcmp (sc->mode, "unlimited")) {
        flux_log_error (h, "invalid mode specified");
        goto error;
    }
    if (!(sc->jobs = zlistx_new ())) {
        flux_log_error (h, "zlistx_new");
        goto error;
    }
    zlistx_set_comparator (sc->jobs, job_cmp);
    zlistx_set_destructor (sc->jobs, job_destructor);

    /* N.B. schedutil_create() registers the "sched" service  name */
    if (flux_msg_handler_addvec (h, htab, sc, &sc->handlers) < 0) {
        flux_log_error (h, "flux_msg_handler_addvec");
        goto error;
    }

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
    if (schedutil_hello (sc->schedutil_ctx, hello_cb, sc) < 0) {
        flux_log_error (h, "schedutil_hello");
        goto done;
    }
    if (schedutil_ready (sc->schedutil_ctx, sc->mode, &count) < 0) {
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
