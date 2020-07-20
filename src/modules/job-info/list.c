/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* list.c - list jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "idsync.h"
#include "list.h"
#include "job_util.h"
#include "job_state.h"

json_t *get_job_by_id (struct info_ctx *ctx,
                       job_info_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall);

/* Filter test to determine if job desired by caller */
bool job_filter (struct job *job, uint32_t userid, int states, int results)
{
    if (!(job->state & states))
        return false;
    if (userid != FLUX_USERID_UNKNOWN && job->userid != userid)
        return false;
    if (job->state & FLUX_JOB_INACTIVE
        && !(job->result & results))
        return false;
    return true;
}

/* Put jobs from list onto jobs array, breaking if max_entries has
 * been reached. Returns 1 if jobs array is full, 0 if continue, -1
 * one error with errno set:
 *
 * ENOMEM - out of memory
 */
int get_jobs_from_list (json_t *jobs,
                        job_info_error_t *errp,
                        zlistx_t *list,
                        int max_entries,
                        json_t *attrs,
                        uint32_t userid,
                        int states,
                        int results)
{
    struct job *job;

    job = zlistx_first (list);
    while (job) {
        if (job_filter (job, userid, states, results)) {
            json_t *o;
            if (!(o = job_to_json (job, attrs, errp)))
                return -1;
            if (json_array_append_new (jobs, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                return -1;
            }
            if (json_array_size (jobs) == max_entries)
                return 1;
        }
        job = zlistx_next (list);
    }

    return 0;
}

/* Create a JSON array of 'job' objects.  'max_entries' determines the
 * max number of jobs to return, 0=unlimited.  Returns JSON object
 * which the caller must free.  On error, return NULL with errno set:
 *
 * EPROTO - malformed or empty attrs array, max_entries out of range
 * ENOMEM - out of memory
 */
json_t *get_jobs (struct info_ctx *ctx,
                  job_info_error_t *errp,
                  int max_entries,
                  json_t *attrs,
                  uint32_t userid,
                  int states,
                  int results)
{
    json_t *jobs = NULL;
    int saved_errno;
    int ret = 0;

    if (!(jobs = json_array ()))
        goto error_nomem;

    /* We return jobs in the following order, pending, running,
     * inactive */

    if (states & FLUX_JOB_PENDING) {
        if ((ret = get_jobs_from_list (jobs,
                                       errp,
                                       ctx->jsctx->pending,
                                       max_entries,
                                       attrs,
                                       userid,
                                       states,
                                       results)) < 0)
            goto error;
    }

    if (states & FLUX_JOB_RUNNING) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           ctx->jsctx->running,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states,
                                           results)) < 0)
                goto error;
        }
    }

    if (states & FLUX_JOB_INACTIVE) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           ctx->jsctx->inactive,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states,
                                           results)) < 0)
                goto error;
        }
    }

    return jobs;

error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (jobs);
    errno = saved_errno;
    return NULL;
}

void list_cb (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    job_info_error_t err;
    json_t *jobs = NULL;
    json_t *attrs;
    int max_entries;
    uint32_t userid;
    int states;
    int results;

    if (flux_request_unpack (msg, NULL, "{s:i s:o s:i s:i s:i}",
                             "max_entries", &max_entries,
                             "attrs", &attrs,
                             "userid", &userid,
                             "states", &states,
                             "results", &results) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }
    if (max_entries < 0) {
        seterror (&err, "invalid payload: max_entries < 0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }
    /* If user sets no states, assume they want all information */
    if (!states)
        states = (FLUX_JOB_PENDING
                  | FLUX_JOB_RUNNING
                  | FLUX_JOB_INACTIVE);

    /* If user sets no results, assume they want all information */
    if (!results)
        results = (FLUX_JOB_RESULT_COMPLETED
                   | FLUX_JOB_RESULT_FAILED
                   | FLUX_JOB_RESULT_CANCELLED
                   | FLUX_JOB_RESULT_TIMEOUT);

    if (!(jobs = get_jobs (ctx, &err, max_entries,
                           attrs, userid, states, results)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (jobs);
}

/* Create a JSON array of 'job' objects.  'since' limits entries
 * returned, only returning entries with 't_inactive' newer than the
 * timestamp.  Returns JSON object which the caller must free.  On
 * error, return NULL with errno set:
 *
 * EPROTO - malformed or empty attrs array
 * ENOMEM - out of memory
 */
json_t *get_inactive_jobs (struct info_ctx *ctx,
                           job_info_error_t *errp,
                           int max_entries,
                           double since,
                           json_t *attrs,
                           const char *name)
{
    json_t *jobs = NULL;
    struct job *job;
    int saved_errno;

    if (!(jobs = json_array ()))
        goto error_nomem;

    job = zlistx_first (ctx->jsctx->inactive);
    while (job && (job->t_inactive > since)) {
        json_t *o;
        if (!name || strcmp (job->name, name) == 0) {
            if (!(o = job_to_json (job, attrs, errp)))
                goto error;
            if (json_array_append_new (jobs, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
            if (json_array_size (jobs) == max_entries)
                goto out;
        }
        job = zlistx_next (ctx->jsctx->inactive);
    }

out:
    return jobs;

error_nomem:
    errno = ENOMEM;
error:
    saved_errno = errno;
    json_decref (jobs);
    errno = saved_errno;
    return NULL;
}

void list_inactive_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    job_info_error_t err = {{0}};
    json_t *jobs = NULL;
    int max_entries;
    double since;
    json_t *attrs;
    const char *name = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i s:F s:o s?:s}",
                             "max_entries", &max_entries,
                             "since", &since,
                             "attrs", &attrs,
                             "name", &name) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        goto error;
    }
    if (max_entries < 0) {
        seterror (&err, "invalid payload: max_entries < 0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }
    if (!(jobs = get_inactive_jobs (ctx, &err,
                                    max_entries,
                                    since,
                                    attrs,
                                    name)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (jobs);
}

int wait_id_valid (struct info_ctx *ctx, struct idsync_data *isd)
{
    zlistx_t *list_isd;
    void *handle;
    int saved_errno;

    if ((handle = zlistx_find (ctx->idsync_lookups, isd))) {
        /* detach will not call zlistx destructor */
        zlistx_detach (ctx->idsync_lookups, handle);
    }

    /* idsync_waits holds lists of ids waiting on, b/c multiplers callers
     * could wait on same id */
    if (!(list_isd = zhashx_lookup (ctx->idsync_waits, &isd->id))) {
        if (!(list_isd = zlistx_new ())) {
            flux_log_error (isd->ctx->h, "%s: zlistx_new", __FUNCTION__);
            goto error_destroy;
        }
        zlistx_set_destructor (list_isd, idsync_data_destroy_wrapper);

        if (zhashx_insert (ctx->idsync_waits, &isd->id, list_isd) < 0) {
            flux_log_error (isd->ctx->h, "%s: zhashx_insert", __FUNCTION__);
            goto error_destroy;
        }
    }

    if (!zlistx_add_end (list_isd, isd)) {
        flux_log_error (isd->ctx->h, "%s: zlistx_add_end", __FUNCTION__);
        goto error_destroy;
    }

    return 0;

error_destroy:
    saved_errno = errno;
    idsync_data_destroy (isd);
    errno = saved_errno;
    return -1;
}

void check_id_valid_continuation (flux_future_t *f, void *arg)
{
    struct idsync_data *isd = arg;
    struct info_ctx *ctx = isd->ctx;
    void *handle;

    if (flux_future_get (f, NULL) < 0) {
        if (flux_respond_error (ctx->h, isd->msg, errno, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto cleanup;
    }
    else {
        /* Job ID is legal.  Chance job-info has seen ID since this
         * lookup was done */
        struct job *job;
        if (!(job = zhashx_lookup (ctx->jsctx->index, &isd->id))
            || job->state == FLUX_JOB_NEW) {
            /* Must wait for job-info to see state change */
            if (wait_id_valid (ctx, isd) < 0)
                flux_log_error (ctx->h, "%s: wait_id_valid", __FUNCTION__);
            goto cleanup;
        }
        else {
            json_t *o;
            if (!(o = get_job_by_id (ctx, NULL, isd->msg,
                                     isd->id, isd->attrs, NULL))) {
                flux_log_error (ctx->h, "%s: get_job_by_id", __FUNCTION__);
                goto cleanup;
            }
            if (flux_respond_pack (ctx->h, isd->msg, "{s:O}", "job", o) < 0) {
                flux_log_error (ctx->h, "%s: flux_respond_pack", __FUNCTION__);
                goto cleanup;
            }
        }
    }

cleanup:
    /* delete will destroy struct idsync_data and future within it */
    handle = zlistx_find (ctx->idsync_lookups, isd);
    if (handle)
        zlistx_delete (ctx->idsync_lookups, handle);
    return;
}

int check_id_valid (struct info_ctx *ctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    json_t *attrs)
{
    flux_future_t *f = NULL;
    struct idsync_data *isd = NULL;
    char path[256];
    int saved_errno;

    /* Check to see if the ID is legal, job-info may have not yet
     * seen the ID publication yet */
    if (flux_job_kvs_key (path, sizeof (path), id, NULL) < 0)
        goto error;

    if (!(f = flux_kvs_lookup (ctx->h, NULL, FLUX_KVS_READDIR, path))) {
        flux_log_error (ctx->h, "%s: flux_kvs_lookup", __FUNCTION__);
        goto error;
    }

    if (!(isd = idsync_data_create (ctx, id, msg, attrs, f)))
        goto error;

    /* future now owned by struct idsync_data */
    f = NULL;

    if (flux_future_then (isd->f_lookup,
                          -1,
                          check_id_valid_continuation,
                          isd) < 0) {
        flux_log_error (ctx->h, "%s: flux_future_then", __FUNCTION__);
        goto error;
    }

    if (!zlistx_add_end (ctx->idsync_lookups, isd)) {
        flux_log_error (ctx->h, "%s: zlistx_add_end", __FUNCTION__);
        goto error;
    }

    return 0;

error:
    saved_errno = errno;
    flux_future_destroy (f);
    idsync_data_destroy (isd);
    errno = saved_errno;
    return -1;
}

/* Returns JSON object which the caller must free.  On error, return
 * NULL with errno set:
 *
 * EPROTO - malformed or empty id or attrs array
 * EINVAL - invalid id
 * ENOMEM - out of memory
 */
json_t *get_job_by_id (struct info_ctx *ctx,
                       job_info_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall)
{
    struct job *job;

    if (!(job = zhashx_lookup (ctx->jsctx->index, &id))) {
        if (stall) {
            if (check_id_valid (ctx, msg, id, attrs) < 0) {
                flux_log_error (ctx->h, "%s: check_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    if (job->state == FLUX_JOB_NEW) {
        if (stall) {
            struct idsync_data *isd;
            if (!(isd = idsync_data_create (ctx, id, msg, attrs, NULL))) {
                flux_log_error (ctx->h, "%s: idsync_data_create", __FUNCTION__);
                return NULL;
            }
            /* Must wait for job-info to see state change */
            if (wait_id_valid (ctx, isd) < 0) {
                flux_log_error (ctx->h, "%s: wait_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    return job_to_json (job, attrs, errp);
}

void list_id_cb (flux_t *h, flux_msg_handler_t *mh,
                  const flux_msg_t *msg, void *arg)
{
    struct info_ctx *ctx = arg;
    job_info_error_t err = {{0}};
    json_t *job = NULL;
    flux_jobid_t id;
    json_t *attrs;
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{s:I s:o}",
                             "id", &id,
                             "attrs", &attrs) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }

    if (!json_is_array (attrs)) {
        seterror (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }

    if (!(job = get_job_by_id (ctx, &err, msg, id, attrs, &stall))) {
        /* response handled after KVS lookup complete */
        if (stall)
            goto stall;
        goto error;
    }

    if (flux_respond_pack (h, msg, "{s:O}", "job", job) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    json_decref (job);
stall:
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (job);
}

int list_attrs_append (json_t *a, const char *attr)
{
    json_t *o = json_string (attr);
    if (!o) {
        errno = ENOMEM;
        return -1;
    }
    if (json_array_append_new (a, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void list_attrs_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    const char *attrs[] = { "userid", "priority", "t_submit", "t_depend",
                            "t_sched", "t_run", "t_cleanup", "t_inactive",
                            "state", "name", "ntasks", "nnodes", "ranks",
                            "success", "exception_occurred", "exception_type",
                            "exception_severity", "exception_note", "result",
                            "expiration", "annotations", NULL };
    json_t *a = NULL;
    int i;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    for (i = 0; attrs[i] != NULL; i++) {
        if (list_attrs_append (a, attrs[i]) < 0)
            goto error;
    }

    if (flux_respond_pack (h, msg, "{s:o}", "attrs", a) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (a);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
