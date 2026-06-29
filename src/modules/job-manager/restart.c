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
#include <flux/core.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "restart.h"
#include "event.h"
#include "wait.h"
#include "queue.h"
#include "jobtap-internal.h"

/* Number of jobs whose KVS lookups are kept in flight while reloading jobs
 * on restart. Pipelining hides KVS round-trip latency, which otherwise
 * dominates restart of an instance with many inactive jobs. The benefit
 * saturates at a small window; larger windows increase memory use and can
 * swamp the KVS/broker message queues. Carried in struct job_loader so it
 * could be made configurable in the future.
 */
#define JOB_LOOKUP_WINDOW 64

/* The KVS lookups for one job, issued but not yet consumed. */
struct job_lookup {
    flux_jobid_t id;
    char *key;                  // KVS dir key, retained for move_to_lost_found
    flux_future_t *eventlog;
    flux_future_t *jobspec;
    flux_future_t *R;
};

/* Sliding window of in-flight job lookups. Jobs are added in KVS walk order
 * (== jobid order) and completed in the same order, so replay order matches
 * the original serial implementation.
 */
struct job_loader {
    struct job_manager *ctx;
    zlistx_t *window;           // FIFO of struct job_lookup *
    int window_size;
    int count;                  // jobs successfully loaded
};

/* restart_map callback, invoked once per job directory found in the KVS.
 * 'id' and 'key' identify the job; the callback fetches and replays it.
 * Returns -1 on error to stop the map, or 0 on success.
 */
typedef int (*restart_map_f)(flux_jobid_t id,
                             const char *key,
                             void *arg,
                             flux_error_t *error);

const char *checkpoint_key = "checkpoint.job-manager";

#define CHECKPOINT_VERSION 1

int restart_count_char (const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s++ == c)
            count++;
    }
    return count;
}

static flux_future_t *lookup_job_data (flux_t *h,
                                       flux_jobid_t id,
                                       const char *key)
{
    char path[64];
    flux_future_t *f;

    if (flux_job_kvs_key (path, sizeof (path), id, key) < 0
        || !(f = flux_kvs_lookup (h, NULL, 0, path)))
        return NULL;
    return f;
}

static const char *lookup_job_data_get (flux_future_t *f,
                                        flux_error_t *error)
{
    const char *result;

    if (flux_kvs_lookup_get (f, &result) < 0) {
        errprintf (error,
                   "lookup %s: %s",
                   flux_kvs_lookup_get_key (f),
                   strerror (errno));
        return NULL;
    }
    return result;
}

/* A job could not be reloaded due to some problem like a truncated eventlog.
 * Move job data to job-lost+found for manual cleanup.
 */
static void move_to_lost_found (flux_t *h, const char *key, flux_jobid_t id)
{
    char nkey[128];
    flux_future_t *f;

    snprintf (nkey, sizeof (nkey), "job-lost+found.job.%s", idf58 (id));
    if (!(f = flux_kvs_move (h, NULL, key, NULL, nkey, 0))
        || flux_future_get (f, NULL) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "mv %s %s: %s",
                  key,
                  nkey,
                  future_strerror (f, errno));
    }
    flux_future_destroy (f);
}

/* Decode the job ID from a leaf KVS directory key and hand it to the map
 * callback, which is responsible for fetching and replaying the job.
 * Return value is the callback's: 1 if a job was loaded, 0 on non-fatal
 * skip, or -1 on a fatal error that will prevent flux from starting.
 */
static int depthfirst_map_one (flux_t *h,
                               const char *key,
                               int dirskip,
                               restart_map_f cb,
                               void *arg,
                               flux_error_t *error)
{
    flux_jobid_t id;

    if (strlen (key) <= dirskip) {
        errprintf (error, "internal error key=%s dirskip=%d", key, dirskip);
        errno = EINVAL;
        return -1;
    }
    if (fluid_decode (key + dirskip + 1, &id, FLUID_STRING_DOTHEX) < 0) {
        errprintf (error, "could not decode %s to job ID", key + dirskip + 1);
        return -1;
    }
    return cb (id, key, arg, error);
}

static int depthfirst_map (flux_t *h,
                           const char *key,
                           int dirskip,
                           restart_map_f cb,
                           void *arg,
                           flux_error_t *error)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = restart_count_char (key + dirskip, '.');
    if (!(f = flux_kvs_lookup (h, NULL, FLUX_KVS_READDIR, key))) {
        errprintf (error,
                   "cannot send lookup request for %s: %s",
                   key,
                   strerror (errno));
        return -1;
    }
    if (flux_kvs_lookup_get_dir (f, &dir) < 0) {
        if (errno == ENOENT && path_level == 0)
            rc = 0;
        else {
            errprintf (error,
                       "could not look up %s: %s",
                       key,
                       strerror (errno));
        }
        goto done;
    }
    if (!(itr = flux_kvsitr_create (dir))) {
        errprintf (error,
                   "could not create iterator for %s: %s",
                   key,
                   strerror (errno));
        goto done;
    }
    while ((name = flux_kvsitr_next (itr))) {
        char *nkey;
        int n;
        if (!flux_kvsdir_isdir (dir, name))
            continue;
        if (!(nkey = flux_kvsdir_key_at (dir, name))) {
            errprintf (error,
                       "could not build key for %s in %s: %s",
                       name,
                       key,
                       strerror (errno));
            goto done_destroyitr;
        }
        if (path_level == 3) // orig 'key' = .A.B.C, thus 'nkey' is complete
            n = depthfirst_map_one (h, nkey, dirskip, cb, arg, error);
        else
            n = depthfirst_map (h, nkey, dirskip, cb, arg, error);
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

static void job_lookup_destroy (struct job_lookup *jl)
{
    if (jl) {
        int saved_errno = errno;
        flux_future_destroy (jl->eventlog);
        flux_future_destroy (jl->jobspec);
        flux_future_destroy (jl->R);
        free (jl->key);
        free (jl);
        errno = saved_errno;
    }
}

/* zlistx_destructor_fn footprint */
static void job_lookup_destructor (void **item)
{
    if (item) {
        job_lookup_destroy (*item);
        *item = NULL;
    }
}

/* Issue (but do not wait on) the KVS lookups for one job. */
static struct job_lookup *job_lookup_create (flux_t *h,
                                             flux_jobid_t id,
                                             const char *key)
{
    struct job_lookup *jl;

    if (!(jl = calloc (1, sizeof (*jl))))
        return NULL;
    jl->id = id;
    if (!(jl->key = strdup (key))
        || !(jl->eventlog = lookup_job_data (h, id, "eventlog"))
        || !(jl->jobspec = lookup_job_data (h, id, "jobspec"))
        || !(jl->R = lookup_job_data (h, id, "R"))) {
        job_lookup_destroy (jl);
        return NULL;
    }
    return jl;
}

static int restart_map_cb (struct job *job,
                           struct job_manager *ctx,
                           flux_error_t *error);

/* Wait on one job's lookups, build the job, and hand it to restart_map_cb.
 * Returns 1 if the job was loaded, 0 if skipped (non-fatal), -1 on fatal
 * error. Lookup/replay failures are non-fatal: the job data is moved aside
 * to job-lost+found. See also: flux-framework/flux-core#6123.
 */
static int job_loader_complete (struct job_loader *loader,
                                struct job_lookup *jl,
                                flux_error_t *error)
{
    flux_t *h = loader->ctx->h;
    const char *eventlog, *jobspec;
    struct job *job;
    flux_error_t e;
    int rc;

    if (!(eventlog = lookup_job_data_get (jl->eventlog, &e))
        || !(jobspec = lookup_job_data_get (jl->jobspec, &e))
        || !(job = job_create_from_eventlog (jl->id,
                                             eventlog,
                                             jobspec,
                                             lookup_job_data_get (jl->R, NULL),
                                             &e))) {
        move_to_lost_found (h, jl->key, jl->id);
        flux_log (h,
                  LOG_ERR,
                  "job %s not replayed: %s",
                  idf58 (jl->id),
                  e.text);
        return 0;
    }
    rc = restart_map_cb (job, loader->ctx, error);
    job_decref (job);
    if (rc < 0)
        return -1;
    loader->count++;
    return 1;
}

/* Complete the oldest in-flight lookup.
 */
static int job_loader_complete_oldest (struct job_loader *loader,
                                       flux_error_t *error)
{
    struct job_lookup *jl;
    int rc;

    if (!(jl = zlistx_first (loader->window)))
        return 0;
    rc = job_loader_complete (loader, jl, error);
    zlistx_delete (loader->window, zlistx_cursor (loader->window)); // frees jl
    return rc;
}

/* restart_map_f callback: issue this job's lookups into the window, draining
 * the oldest first if the window is full.
 */
static int job_loader_add (flux_jobid_t id,
                           const char *key,
                           void *arg,
                           flux_error_t *error)
{
    struct job_loader *loader = arg;
    struct job_lookup *jl;

    if (!(jl = job_lookup_create (loader->ctx->h, id, key))) {
        errprintf (error,
                   "cannot send lookup requests for job %s: %s",
                   idf58 (id),
                   strerror (errno));
        return -1;
    }
    if (!zlistx_add_end (loader->window, jl)) {
        job_lookup_destroy (jl);
        errprintf (error, "out of memory queueing job %s", idf58 (id));
        return -1;
    }
    if (zlistx_size (loader->window) > loader->window_size)
        return job_loader_complete_oldest (loader, error);
    return 0;
}

/* Drain all remaining in-flight lookups, in order. Returns 0 or -1.
 */
static int job_loader_drain (struct job_loader *loader, flux_error_t *error)
{
    while (zlistx_size (loader->window) > 0) {
        if (job_loader_complete_oldest (loader, error) < 0)
            return -1;
    }
    return 0;
}

static int job_loader_init (struct job_loader *loader, struct job_manager *ctx)
{
    memset (loader, 0, sizeof (*loader));
    loader->ctx = ctx;
    loader->window_size = JOB_LOOKUP_WINDOW;
    if (!(loader->window = zlistx_new ()))
        return -1;
    zlistx_set_destructor (loader->window, job_lookup_destructor);
    return 0;
}

static void job_loader_finalize (struct job_loader *loader)
{
    zlistx_destroy (&loader->window); // destroys any leftover job_lookups
}

/* The job state/flags has been recreated by replaying the job's eventlog.
 * Enqueue the job and kick off actions appropriate for job's current state.
 */
static int restart_map_cb (struct job *job,
                           struct job_manager *ctx,
                           flux_error_t *error)
{
    flux_job_state_t state = job->state;

    if (zhashx_insert (ctx->active_jobs, &job->id, job) < 0) {
        errprintf (error,
                   "could not insert job %s into active job hash",
                   idf58 (job->id));
        return -1;
    }
    if (ctx->max_jobid < job->id)
        ctx->max_jobid = job->id;
    if ((job->flags & FLUX_JOB_WAITABLE))
        wait_notify_active (ctx->wait, job);
    if (event_job_action (ctx->event, job) < 0) {
        flux_log_error (ctx->h,
                        "replay warning: %s->%s action failed on job %s",
                        flux_job_statetostr (state, "L"),
                        flux_job_statetostr (job->state, "L"),
                        idf58 (job->id));
    }
    return 0;
}

int restart_save_state_to_txn (struct job_manager *ctx, flux_kvs_txn_t *txn)
{
    json_t *queue;

    if (!(queue = queue_ctx_save (ctx->queue)))
        return -1;
    if (flux_kvs_txn_pack (txn,
                           0,
                           checkpoint_key,
                           "{s:i s:I s:O}",
                           "version", CHECKPOINT_VERSION,
                           "max_jobid", ctx->max_jobid,
                           "queue", queue) < 0) {
        json_decref (queue);
        return -1;
    }
    json_decref (queue);
    return 0;
}

int restart_save_state (struct job_manager *ctx)
{
    flux_future_t *f = NULL;
    flux_kvs_txn_t *txn;
    int rc = -1;

    if (!(txn = flux_kvs_txn_create ())
        || restart_save_state_to_txn (ctx, txn) < 0
        || !(f = flux_kvs_commit (ctx->h, NULL, 0, txn))
        || flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
    return rc;
}

static int restart_restore_state (struct job_manager *ctx)
{
    flux_future_t *f;
    flux_jobid_t id;
    json_t *queue = NULL;
    int version = 0;

    if (!(f = flux_kvs_lookup (ctx->h, NULL, 0, checkpoint_key)))
        return -1;

    if (flux_kvs_lookup_get_unpack (f,
                                    "{s?i s:I s?o}",
                                    "version", &version,
                                    "max_jobid", &id,
                                    "queue", &queue) < 0)
        goto error;
    if (version > 1) {
        errno = EINVAL;
        return -1;
    }
    if (ctx->max_jobid < id)
        ctx->max_jobid = id;
    if (queue) {
        if (queue_ctx_restore (ctx->queue, version, queue) < 0)
            goto error;
    }
    flux_future_destroy (f);
    return 0;
error:
    flux_future_destroy (f);
    return -1;
}

int restart_from_kvs (struct job_manager *ctx)
{
    const char *dirname = "job";
    int dirskip = strlen (dirname);
    struct job *job;
    zlistx_t *active_jobs;
    flux_error_t error;
    struct job_loader loader;

    /* Load any active jobs present in the KVS at startup. The job-loader
     * pipelines the per-job KVS lookups: depthfirst_map walks the job
     * directory in jobid order and feeds each job to job_loader_add, which
     * keeps a window of lookups in flight and completes them in order.
     */
    if (job_loader_init (&loader, ctx) < 0) {
        flux_log_error (ctx->h, "restart: job_loader_init");
        return -1;
    }
    if (depthfirst_map (ctx->h,
                        dirname,
                        dirskip,
                        job_loader_add,
                        &loader,
                        &error) < 0
        || job_loader_drain (&loader, &error) < 0) {
        flux_log (ctx->h, LOG_ERR, "restart failed: %s", error.text);
        job_loader_finalize (&loader);
        return -1;
    }
    flux_log (ctx->h, LOG_INFO, "restart: %d jobs", loader.count);
    job_loader_finalize (&loader);

    /* Get active jobs as list for safe iteration:
     */
    if (!(active_jobs = zhashx_values (ctx->active_jobs))) {
        flux_log (ctx->h, LOG_ERR, "restart: failed to get active_jobs list");
        return -1;
    }

    /* Post flux-restart to any jobs in SCHED state, so they may
     * transition back to PRIORITY and re-obtain the priority.
     *
     * Initialize the count of "running" jobs
     */
    job = zlistx_first (active_jobs);
    while (job) {
        if (job->state == FLUX_JOB_STATE_NEW
            || job->state == FLUX_JOB_STATE_DEPEND) {
            flux_error_t errmsg;
            if (jobtap_check_dependencies (ctx->jobtap,
                                           job,
                                           true,
                                           &errmsg) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "restart: id=%s: dependency check failed: %s",
                          idf58 (job->id), errmsg.text);
            }
        }
        /*
         *  On restart, call 'job.create' and 'job.new' plugin callbacks
         *   since this is the first time this instance of the job-manager
         *   has seen this job. Be sure to call these before posting any
         *   other events below, since these should always be the first
         *   callbacks for a job.
         *
         *  Jobs in SCHED state may also immediately transition back to
         *   PRIORITY, potentially generating two other plugin callbacks
         *   after this one. (job.priority, job.sched...)
         */
        if (jobtap_call (ctx->jobtap, job, "job.create", NULL) < 0)
            flux_log_error (ctx->h, "jobtap_call (id=%s, create)",
                            idf58 (job->id));
        if (jobtap_call (ctx->jobtap, job, "job.new", NULL) < 0)
            flux_log_error (ctx->h, "jobtap_call (id=%s, new)",
                            idf58 (job->id));

        if (job->state == FLUX_JOB_STATE_SCHED) {
            /*
             * This is confusing. In order to update priority on transition
             *  back to PRIORITY state, the priority must be reset to "-1",
             *  even though the last priority value was reconstructed from
             *  the eventlog. This is because the transitioning "priority"
             *  event is only posted when the priority changes.
             */
            job->priority = -1;
            if (event_job_post_pack (ctx->event,
                                     job,
                                     "flux-restart",
                                     0,
                                     NULL) < 0) {
                flux_log_error (ctx->h, "%s: event_job_post_pack id=%s",
                                __FUNCTION__, idf58 (job->id));
            }
        }
        else if ((job->state & FLUX_JOB_STATE_RUNNING) != 0) {
            ctx->running_jobs++;
            job->reattach = 1;
            if ((job->flags & FLUX_JOB_DEBUG)) {
                if (event_job_post_pack (ctx->event,
                                         job,
                                         "debug.exec-reattach-start",
                                         0,
                                         "{s:I}",
                                         "id", idf58 (job->id)) < 0)
                    flux_log_error (ctx->h, "%s: event_job_post_pack id=%s",
                                    __FUNCTION__, idf58 (job->id));
            }
        }
        job = zlistx_next (active_jobs);
    }
    zlistx_destroy (&active_jobs);
    flux_log (ctx->h, LOG_INFO, "restart: %d running jobs", ctx->running_jobs);

    /* Restore misc state.
     */
    if (restart_restore_state (ctx) < 0) {
        if (errno != ENOENT) {
            flux_log_error (ctx->h, "restart: %s", checkpoint_key);
            return -1;
        }
        flux_log (ctx->h, LOG_INFO, "restart: %s not found", checkpoint_key);
    }
    flux_log (ctx->h,
              LOG_DEBUG,
              "restart: max_jobid=%s",
              idf58 (ctx->max_jobid));
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
