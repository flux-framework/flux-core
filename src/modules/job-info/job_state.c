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

#include "job_state.h"

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Hash numerical jobid in 'key'.
 * N.B. zhashx_hash_fn signature
 */
static size_t job_hasher (const void *key)
{
    const flux_jobid_t *id = key;
    return *id;
}

/* Compare hash keys.
 * N.B. zhashx_comparator_fn signature
 */
static int job_hash_key_cmp (const void *key1, const void *key2)
{
    const flux_jobid_t *id1 = key1;
    const flux_jobid_t *id2 = key2;

    return NUMCMP (*id1, *id2);
}

/* Compare items for sorting in list.
 * N.B. zlistx_comparator_fn signature
 */
static int job_list_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->t_submit, j2->t_submit);
    return rc;
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

    if (!(jsctx->index = zhashx_new ()))
        goto error;
    zhashx_set_destructor (jsctx->index, job_destroy_wrapper);
    zhashx_set_key_hasher (jsctx->index, job_hasher);
    zhashx_set_key_comparator (jsctx->index, job_hash_key_cmp);
    zhashx_set_key_duplicator (jsctx->index, NULL);
    zhashx_set_key_destructor (jsctx->index, NULL);

    if (!(jsctx->pending = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->pending, job_list_cmp);

    if (!(jsctx->running = zlistx_new ()))
        goto error;

    if (!(jsctx->inactive = zlistx_new ()))
        goto error;

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
