/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_state.c - store information on state of jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libjob/job_hash.h"

#include "job_state.h"

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Compare items for sorting in list, priority first, t_submit second
 * N.B. zlistx_comparator_fn signature
 */
static int job_priority_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->t_submit, j2->t_submit);
    return rc;
}

/* Compare items for sorting in list by timestamp (note that sorting
 * is in reverse order, most recently running/completed comes first).
 * N.B. zlistx_comparator_fn signature
 */
static int job_running_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_running, j1->t_running);
}

static int job_inactive_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_inactive, j1->t_inactive);
}

static void job_destroy (void *data)
{
    struct job *job = data;
    if (job)
        free (job);
}

static void job_destroy_wrapper (void **data)
{
    struct job **job = (struct job **)data;
    job_destroy (*job);
}

static struct job *job_create (struct info_ctx *ctx, flux_jobid_t id)
{
    struct job *job = NULL;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->ctx = ctx;
    job->id = id;
    return job;
}

struct job_state_ctx *job_state_create (flux_t *h)
{
    struct job_state_ctx *jsctx = NULL;
    int saved_errno;

    if (!(jsctx = calloc (1, sizeof (*jsctx)))) {
        flux_log_error (h, "calloc");
        return NULL;
    }
    jsctx->h = h;

    /* Index is the primary data structure holding the job data
     * structures.  It is responsible for destruction.  Lists only
     * contain desired sort of jobs.
     */

    if (!(jsctx->index = job_hash_create ()))
        goto error;
    zhashx_set_destructor (jsctx->index, job_destroy_wrapper);

    if (!(jsctx->pending = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->pending, job_priority_cmp);

    if (!(jsctx->running = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->running, job_running_cmp);

    if (!(jsctx->inactive = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->inactive, job_inactive_cmp);

    if (!(jsctx->processing = zlistx_new ()))
        goto error;

    if (!(jsctx->futures = zlistx_new ()))
        goto error;

    if (flux_event_subscribe (h, "job-state") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto error;
    }

    return jsctx;

error:
    saved_errno = errno;
    job_state_destroy (jsctx);
    errno = saved_errno;
    return NULL;
}

void job_state_destroy (void *data)
{
    struct job_state_ctx *jsctx = data;
    if (jsctx) {
        /* Don't destroy processing until futures are complete */
        if (jsctx->futures) {
            flux_future_t *f;
            f = zlistx_first (jsctx->futures);
            while (f) {
                if (flux_future_get (f, NULL) < 0)
                    flux_log_error (jsctx->h, "%s: flux_future_get",
                                    __FUNCTION__);
                flux_future_destroy (f);
                f = zlistx_next (jsctx->futures);
            }
            zlistx_destroy (&jsctx->futures);
        }
        /* Destroy index last, as it is the one that will actually
         * destroy the job objects */
        if (jsctx->processing)
            zlistx_destroy (&jsctx->processing);
        if (jsctx->inactive)
            zlistx_destroy (&jsctx->inactive);
        if (jsctx->running)
            zlistx_destroy (&jsctx->running);
        if (jsctx->pending)
            zlistx_destroy (&jsctx->pending);
        if (jsctx->index)
            zhashx_destroy (&jsctx->index);
        (void)flux_event_unsubscribe (jsctx->h, "job-state");
        free (jsctx);
    }
}

/* zlistx_insert() and zlistx_reorder() take a 'low_value' parameter
 * which indicates which end of the list to search from.
 * false=search begins at tail (lowest priority, youngest)
 * true=search begins at head (highest priority, oldest)
 * Attempt to minimize search distance based on job priority.
 */
static bool search_direction (struct job *job)
{
    if (job->priority > FLUX_JOB_PRIORITY_DEFAULT)
        return true;
    else
        return false;
}

static void job_insert_list (struct job_state_ctx *jsctx,
                             struct job *job,
                             flux_job_state_t newstate)
{
    /* Note: comparator is set for running & inactive lists, but the
     * sort calls are not called on zlistx_add_start() */
    if (newstate == FLUX_JOB_DEPEND
        || newstate == FLUX_JOB_SCHED) {
        if (!(job->list_handle = zlistx_insert (jsctx->pending,
                                                job,
                                                search_direction (job))))
            flux_log_error (jsctx->h, "%s: zlistx_insert",
                            __FUNCTION__);
    }
    else if (newstate == FLUX_JOB_RUN
             || newstate == FLUX_JOB_CLEANUP) {
        if (!(job->list_handle = zlistx_add_start (jsctx->running,
                                                   job)))
            flux_log_error (jsctx->h, "%s: zlistx_add_start",
                            __FUNCTION__);
    }
    else { /* newstate == FLUX_JOB_INACTIVE */
        if (!(job->list_handle = zlistx_add_start (jsctx->inactive,
                                                   job)))
            flux_log_error (jsctx->h, "%s: zlistx_add_start",
                            __FUNCTION__);
    }
}

/* remove job from one list and move it to another based on the
 * newstate */
static void job_change_list (struct job_state_ctx *jsctx,
                             struct job *job,
                             zlistx_t *oldlist,
                             flux_job_state_t newstate)
{
    if (zlistx_detach (oldlist, job->list_handle) < 0)
        flux_log_error (jsctx->h, "%s: zlistx_detach",
                        __FUNCTION__);
    job->list_handle = NULL;

    job_insert_list (jsctx, job, newstate);
}

static void eventlog_lookup_continuation (flux_future_t *f, void *arg)
{
    struct job *job = arg;
    struct info_ctx *ctx = job->ctx;
    const char *s;
    json_t *a = NULL;
    size_t index;
    json_t *value;
    void *handle;

    if (flux_rpc_get_unpack (f, "{s:s}", "eventlog", &s) < 0) {
        flux_log_error (ctx->h, "%s: error eventlog for %llu",
                        __FUNCTION__, (unsigned long long)job->id);
        return;
    }

    if (!(a = eventlog_decode (s))) {
        flux_log_error (ctx->h, "%s: error parsing eventlog for %llu",
                        __FUNCTION__, (unsigned long long)job->id);
        return;
    }

    json_array_foreach (a, index, value) {
        const char *name;
        double timestamp;
        json_t *context = NULL;

        if (eventlog_entry_parse (value, &timestamp, &name, &context) < 0) {
            flux_log_error (ctx->h, "%s: error parsing entry for %llu",
                            __FUNCTION__, (unsigned long long)job->id);
            goto out;
        }

        if (!strcmp (name, "submit")) {
            if (!context) {
                flux_log_error (ctx->h, "%s: no submit context for %llu",
                                __FUNCTION__, (unsigned long long)job->id);
                goto out;
            }

            if (json_unpack (context, "{ s:i s:i s:i }",
                             "priority", &job->priority,
                             "userid", &job->userid,
                             "flags", &job->flags) < 0) {
                flux_log_error (ctx->h, "%s: submit context for %llu invalid",
                                __FUNCTION__, (unsigned long long)job->id);
                goto out;
            }
            job->t_submit = timestamp;
            job->job_info_retrieved = true;

            /* move from processing to appropriate list */
            job_change_list (ctx->jsctx,
                             job,
                             ctx->jsctx->processing,
                             job->state);
        }
    }

out:
    json_decref (a);
    handle = zlistx_find (ctx->jsctx->futures, f);
    if (handle)
        zlistx_detach (ctx->jsctx->futures, handle);
    flux_future_destroy (f);
}

static flux_future_t *eventlog_lookup (struct job_state_ctx *jsctx,
                                       struct job *job)
{
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(f = flux_rpc_pack (jsctx->h, "job-info.lookup", FLUX_NODEID_ANY, 0,
                             "{s:I s:[s] s:i}",
                             "id", job->id,
                             "keys", "eventlog",
                             "flags", 0))) {
        flux_log_error (jsctx->h, "%s: flux_rpc_pack", __FUNCTION__);
        goto error;
    }

    if (flux_future_then (f, -1, eventlog_lookup_continuation, job) < 0) {
        flux_log_error (jsctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    return f;

 error:
    saved_errno = errno;
    flux_future_destroy (f);
    errno = saved_errno;
    return NULL;
}

static zlistx_t *get_list (struct job_state_ctx *jsctx, flux_job_state_t state)
{
    if (state == FLUX_JOB_DEPEND
        || state == FLUX_JOB_SCHED)
        return jsctx->pending;
    else if (state == FLUX_JOB_RUN
             || state == FLUX_JOB_CLEANUP)
        return jsctx->running;
    else /* state == FLUX_JOB_INACTIVE */
        return jsctx->inactive;
}

static void update_job_state (struct job *job, flux_job_state_t newstate)
{
    struct job_state_ctx *jsctx = job->ctx->jsctx;

    if (!job->job_info_retrieved) {
        /* job info still not retrieved, we can update the job
         * state but can't put it on a different list yet */
        job->state = newstate;
    }
    else {
        if (job->state == FLUX_JOB_INACTIVE) {
            flux_log_error (jsctx->h,
                            "%s: illegal transition: id=%llu state=%d",
                            __FUNCTION__, (unsigned long long)job->id, newstate);
        }
        else {
            zlistx_t *oldlist, *newlist;

            oldlist = get_list (jsctx, job->state);
            newlist = get_list (jsctx, newstate);

            if (oldlist != newlist)
                job_change_list (jsctx, job, oldlist, newstate);
            job->state = newstate;
        }
    }
}

static void update_jobs (struct info_ctx *ctx, json_t *transitions)
{
    struct job_state_ctx *jsctx = ctx->jsctx;
    size_t index;
    json_t *value;

    if (!json_is_array (transitions)) {
        flux_log_error (ctx->h, "%s: transitions EPROTO", __FUNCTION__);
        return;
    }

    json_array_foreach (transitions, index, value) {
        struct job *job;
        json_t *o;
        flux_jobid_t id;
        flux_job_state_t state;

        if (!json_is_array (value)) {
            flux_log_error (jsctx->h, "%s: transition EPROTO", __FUNCTION__);
            return;
        }

        if (!(o = json_array_get (value, 0))
            || !json_is_integer (o)) {
            flux_log_error (jsctx->h, "%s: transition EPROTO", __FUNCTION__);
            return;
        }

        id = json_integer_value (o);

        if (!(o = json_array_get (value, 1))
            || !json_is_string (o)) {
            flux_log_error (jsctx->h, "%s: transition EPROTO", __FUNCTION__);
            return;
        }

        if (flux_job_strtostate (json_string_value (o), &state) < 0) {
            flux_log_error (jsctx->h, "%s: transition EPROTO", __FUNCTION__);
            return;
        }

        if (!(job = zhashx_lookup (jsctx->index, &id))) {
            flux_future_t *f = NULL;
            void *handle;
            if (!(job = job_create (ctx, id))){
                flux_log_error (jsctx->h, "%s: job_create", __FUNCTION__);
                return;
            }
            if (zhashx_insert (jsctx->index, &job->id, job) < 0) {
                flux_log_error (jsctx->h, "%s: zhashx_insert", __FUNCTION__);
                job_destroy (job);
                return;
            }

            /* initial state transition does not provide information
             * like userid, priority, t_submit, and flags.  We have to
             * go get this information from the eventlog */
            if (!(f = eventlog_lookup (jsctx, job))) {
                flux_log_error (jsctx->h, "%s: eventlog_lookup", __FUNCTION__);
                return;
            }

            if (!(handle = zlistx_add_end (jsctx->futures, f))) {
                flux_log_error (jsctx->h, "%s: zlistx_add_end", __FUNCTION__);
                flux_future_destroy (f);
                return;
            }

            if (!(job->list_handle = zlistx_add_end (jsctx->processing, job))) {
                flux_log_error (jsctx->h, "%s: zlistx_add_end", __FUNCTION__);
                return;
            }
            job->state = state;
        }
        else
            update_job_state (job, state);
    }

}

void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                   const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    json_t *transitions;

    if (flux_event_unpack (msg, NULL, "{s:o}",
                           "transitions",
                           &transitions) < 0) {
        flux_log_error (h, "%s: flux_event_unpack", __FUNCTION__);
        return;
    }

    update_jobs (ctx, transitions);

    return;
}

static struct job *eventlog_parse (struct info_ctx *ctx,
                                   const char *eventlog,
                                   flux_jobid_t id)
{
    struct job *job = NULL;
    json_t *a = NULL;
    size_t index;
    json_t *value;

    if (!(job = job_create (ctx, id)))
        goto error;
    job->state = FLUX_JOB_NEW;

    if (!(a = eventlog_decode (eventlog))) {
        flux_log_error (ctx->h, "%s: error parsing eventlog for %llu",
                        __FUNCTION__, (unsigned long long)job->id);
        goto error;
    }

    json_array_foreach (a, index, value) {
        const char *name;
        double timestamp;
        json_t *context = NULL;

        if (eventlog_entry_parse (value, &timestamp, &name, &context) < 0) {
            flux_log_error (ctx->h, "%s: error parsing entry for %llu",
                            __FUNCTION__, (unsigned long long)job->id);
            goto error;
        }

        if (!strcmp (name, "submit")) {
            if (!context) {
                flux_log_error (ctx->h, "%s: no submit context for %llu",
                                __FUNCTION__, (unsigned long long)job->id);
                goto error;
            }

            if (json_unpack (context, "{ s:i s:i s:i }",
                             "priority", &job->priority,
                             "userid", &job->userid,
                             "flags", &job->flags) < 0) {
                flux_log_error (ctx->h, "%s: submit context for %llu invalid",
                                __FUNCTION__, (unsigned long long)job->id);
                goto error;
            }
            job->t_submit = timestamp;
            job->job_info_retrieved = true;
            job->state = FLUX_JOB_DEPEND;
        }
        else if (!strcmp (name, "depend")) {
            job->state = FLUX_JOB_SCHED;
        }
        else if (!strcmp (name, "priority")) {
            if (json_unpack (context, "{ s:i }",
                                      "priority", &job->priority) < 0) {
                flux_log_error (ctx->h, "%s: priority context for %llu invalid",
                                __FUNCTION__, (unsigned long long)job->id);
                goto error;
            }
        }
        else if (!strcmp (name, "exception")) {
            int severity;
            if (json_unpack (context, "{ s:i }", "severity", &severity) < 0) {
                flux_log_error (ctx->h, "%s: exception context for %llu invalid",
                                __FUNCTION__, (unsigned long long)job->id);
                goto error;
            }
            if (severity == 0) {
                job->state = FLUX_JOB_CLEANUP;
                job->t_inactive = timestamp;
            }
        }
        else if (!strcmp (name, "alloc")) {
            if (job->state == FLUX_JOB_SCHED) {
                job->state = FLUX_JOB_RUN;
                job->t_running = timestamp;
            }
        }
        else if (!strcmp (name, "finish")) {
            if (job->state == FLUX_JOB_RUN)
                job->state = FLUX_JOB_CLEANUP;
        }
        else if (!strcmp (name, "clean")) {
            job->state = FLUX_JOB_INACTIVE;
            job->t_inactive = timestamp;
        }
    }

    if (job->state == FLUX_JOB_NEW) {
        flux_log_error (ctx->h, "%s: eventlog has no transition events",
                        __FUNCTION__);
        goto error;
    }

    json_decref (a);
    return job;

error:
    job_destroy (job);
    json_decref (a);
    return NULL;
}

static int depthfirst_count_depth (const char *s)
{
    int count = 0;
    while (*s) {
        if (*s++ == '.')
            count++;
    }
    return count;
}

static int depthfirst_map_one (struct info_ctx *ctx, const char *key,
                               int dirskip)
{
    struct job *job = NULL;
    flux_jobid_t id;
    flux_future_t *f;
    const char *eventlog;
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
    if (!(f = flux_kvs_lookup (ctx->h, NULL, 0, path)))
        goto done;
    if (flux_kvs_lookup_get (f, &eventlog) < 0)
        goto done;

    if ((job = eventlog_parse (ctx, eventlog, id))) {
        if (zhashx_insert (ctx->jsctx->index, &job->id, job) < 0) {
            flux_log_error (ctx->h, "%s: zhashx_insert", __FUNCTION__);
            job_destroy (job);
            goto done;
        }
        job_insert_list (ctx->jsctx, job, job->state);
    }
    rc = 1;
done:
    flux_future_destroy (f);
    return rc;
}

static int depthfirst_map (struct info_ctx *ctx, const char *key,
                           int dirskip)
{
    flux_future_t *f;
    const flux_kvsdir_t *dir;
    flux_kvsitr_t *itr;
    const char *name;
    int path_level;
    int count = 0;
    int rc = -1;

    path_level = depthfirst_count_depth (key + dirskip);
    if (!(f = flux_kvs_lookup (ctx->h, NULL, FLUX_KVS_READDIR, key)))
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
            n = depthfirst_map_one (ctx, nkey, dirskip);
        else
            n = depthfirst_map (ctx, nkey, dirskip);
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

/* Read jobs present in the KVS at startup. */
int job_state_init_from_kvs (struct info_ctx *ctx)
{
    const char *dirname = "job";
    int dirskip = strlen (dirname);
    int count;

    count = depthfirst_map (ctx, dirname, dirskip);
    if (count < 0)
        return -1;
    flux_log (ctx->h, LOG_DEBUG, "%s: read %d jobs", __FUNCTION__, count);

    zlistx_sort (ctx->jsctx->running);
    zlistx_sort (ctx->jsctx->inactive);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
