/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* restart - reload active jobs from the KVS */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <argz.h>
#include <envz.h>
#include <flux/core.h>

#include "src/common/libutil/fluid.h"

#include "job.h"
#include "restart.h"
#include "event.h"
#include "wait.h"

/* restart_map callback should return -1 on error to stop map with error,
 * or 0 on success.  'job' is only valid for the duration of the callback.
 */
typedef int (*restart_map_f)(struct job *job, void *arg);

const char *checkpoint_key = "checkpoint.job-manager";

int restart_count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static int depthfirst_map_one (flux_t *h, const char *key, int dirskip,
                               restart_map_f cb, void *arg)
{
    flux_jobid_t id;
    flux_future_t *f;
    const char *eventlog;
    struct job *job = NULL;
    char path[64];
    int rc = -1;

    if (strlen (key) <= dirskip) {
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0)
        return -1;
    if (flux_job_kvs_key (path, sizeof (path), id, "eventlog") < 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, path)))
        goto done;
    if (flux_kvs_lookup_get (f, &eventlog) < 0)
        goto done;
    if (!(job = job_create_from_eventlog (id, eventlog)))
        goto done;
    if (cb (job, arg) < 0)
        goto done;
    rc = 1;
done:
    flux_future_destroy (f);
    job_decref (job);
    return rc;
}

static int depthfirst_map (flux_t *h, const char *key,
                           int dirskip, restart_map_f cb, void *arg)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = restart_count_char (key + dirskip, '.');
    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_READDIR, key)))
        return -1;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && path_level == 0)
            rc = 0;
        goto done;
    }
    if (!(itr = flux_kvsitr_create (dir)))
        goto done;
    while ((name = flux_kvsitr_next (itr))) {
        char *nkey;
        int n;
        if (!flux_kvsdir_isdir (dir, name))
            continue;
        if (!(nkey = flux_kvsdir_key_at (dir, name)))
            goto done_destroyitr;
        if (path_level == 3) // orig 'key' = .A.B.C, thus 'nkey' is complete
            n = depthfirst_map_one (h, nkey, dirskip, cb, arg);
        else
            n = depthfirst_map (h, nkey, dirskip, cb, arg);
        if (n < 0) {
            int saved_errno = errno;
            free (nkey);
            errno = saved_errno;
            goto done_destroyitr;
        }
        count += n;
        free (nkey);
    }
    rc = count;
done_destroyitr:
    flux_kvsitr_destroy (itr);
done:
    flux_future_destroy (f);
    return rc;
}

/* reload_map_f callback
 * The job state/flags has been recreated by replaying the job's eventlog.
 * Enqueue the job and kick off actions appropriate for job's current state.
 */
static int restart_map_cb (struct job *job, void *arg)
{
    struct job_manager *ctx = arg;

    if (zhashx_insert (ctx->active_jobs, &job->id, job) < 0)
        return -1;
    if ((job->flags & FLUX_JOB_WAITABLE))
        wait_notify_active (ctx->wait, job);
    if (job->state == FLUX_JOB_STATE_SCHED) {
       /* The flux-restart event is posted to jobs in SCHED state to
        * transition back to PRIORITY so that the queue priority can
        * be re-established.
        */
        if (event_job_post_pack (ctx->event,
                                 job,
                                 "flux-restart",
                                 0,
                                 NULL) < 0) {
            flux_log_error (ctx->h, "%s: event_job_post_pack id=%ju",
                            __FUNCTION__, (uintmax_t)job->id);
        }
    }
    else {
        if (event_job_action (ctx->event, job) < 0) {
            flux_log_error (ctx->h, "%s: event_job_action id=%ju",
                            __FUNCTION__, (uintmax_t)job->id);
        }
    }
    return 0;
}

static int checkpoint_save (struct job_manager *ctx)
{
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ()))
        return -1;
    if (flux_kvs_txn_pack (txn,
                           0,
                           checkpoint_key,
                           "{s:I}",
                           "max_jobid",
                           ctx->max_jobid) < 0)
        goto done;
    if (!(f = flux_kvs_commit (ctx->h, NULL, 0, txn)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return rc;
}

static int checkpoint_restore (struct job_manager *ctx)
{
    flux_future_t *f;

    if (!(f = flux_kvs_lookup (ctx->h, NULL, 0, checkpoint_key)))
        return -1;
    if (flux_kvs_lookup_get_unpack (f,
                                    "{s:I}",
                                    "max_jobid",
                                    &ctx->max_jobid) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int restart_from_kvs (struct job_manager *ctx)
{
    const char *dirname = "job";
    int dirskip = strlen (dirname);
    int count;
    struct job *job;

    /* Load any active jobs present in the KVS at startup.
     */
    count = depthfirst_map (ctx->h, dirname, dirskip, restart_map_cb, ctx);
    if (count < 0)
        return -1;
    flux_log (ctx->h, LOG_INFO, "restart: %d jobs", count);
    /* Initialize the count of "running" jobs
     */
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if ((job->state & FLUX_JOB_STATE_RUNNING) != 0)
            ctx->running_jobs++;
        job = zhashx_next (ctx->active_jobs);
    }
    flux_log (ctx->h, LOG_INFO, "restart: %d running jobs", ctx->running_jobs);

    /* Restore misc state.
     */
    if (checkpoint_restore (ctx) < 0) {
        if (errno != ENOENT) {
            flux_log_error (ctx->h, "restart: %s", checkpoint_key);
            return -1;
        }
        flux_log (ctx->h, LOG_INFO, "restart: no checkpoint object");
    }
    flux_log (ctx->h,
              LOG_DEBUG,
              "restart: max_jobid=%ju",
              (uintmax_t)ctx->max_jobid);
    return 0;
}

int checkpoint_to_kvs (struct job_manager *ctx)
{
    if (checkpoint_save (ctx) < 0) {
        flux_log_error (ctx->h, "checkpoint");
        return -1;
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
