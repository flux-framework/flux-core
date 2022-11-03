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
#include <jansson.h>
#include <flux/core.h>
#include <assert.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "job-list.h"
#include "idsync.h"
#include "list.h"
#include "job_util.h"
#include "job_data.h"

json_t *get_job_by_id (struct job_state_ctx *jsctx,
                       job_list_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall);

/* Filter test to determine if job desired by caller */
bool job_filter (struct job *job,
                 uint32_t userid,
                 int states,
                 int results,
                 const char *name,
                 const char *queue)
{
    if (name && (!job->name || !streq (job->name, name)))
        return false;
    if (queue && (!job->queue || !streq (job->queue, queue)))
        return false;
    if (!(job->state & states))
        return false;
    if (userid != FLUX_USERID_UNKNOWN && job->userid != userid)
        return false;
    if (job->state & FLUX_JOB_STATE_INACTIVE
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
                        job_list_error_t *errp,
                        zlistx_t *list,
                        int max_entries,
                        json_t *attrs,
                        uint32_t userid,
                        int states,
                        int results,
                        double since,
                        const char *name,
                        const char *queue)
{
    struct job *job;

    job = zlistx_first (list);
    while (job) {

        /*  If job->t_inactive > 0. (we're on the inactive jobs list),
         *   and job->t_inactive > since, then we're done since inactive
         *   jobs are sorted by inactive time.
         */
        if (job->t_inactive > 0. && job->t_inactive <= since)
            break;

        if (job_filter (job, userid, states, results, name, queue)) {
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
 * max number of jobs to return, 0=unlimited. 'since' limits jobs returned
 * to those with t_inactive greater than timestamp.  Returns JSON object
 * which the caller must free.  On error, return NULL with errno set:
 *
 * EPROTO - malformed or empty attrs array, max_entries out of range
 * ENOMEM - out of memory
 */
json_t *get_jobs (struct job_state_ctx *jsctx,
                  job_list_error_t *errp,
                  int max_entries,
                  double since,
                  json_t *attrs,
                  uint32_t userid,
                  int states,
                  int results,
                  const char *name,
                  const char *queue)
{
    json_t *jobs = NULL;
    int saved_errno;
    int ret = 0;

    if (!(jobs = json_array ()))
        goto error_nomem;

    /* We return jobs in the following order, pending, running,
     * inactive */

    if (states & FLUX_JOB_STATE_PENDING) {
        if ((ret = get_jobs_from_list (jobs,
                                       errp,
                                       jsctx->pending,
                                       max_entries,
                                       attrs,
                                       userid,
                                       states,
                                       results,
                                       0.,
                                       name,
                                       queue)) < 0)
            goto error;
    }

    if (states & FLUX_JOB_STATE_RUNNING) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           jsctx->running,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states,
                                           results,
                                           0.,
                                           name,
                                           queue)) < 0)
                goto error;
        }
    }

    if (states & FLUX_JOB_STATE_INACTIVE) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           jsctx->inactive,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states,
                                           results,
                                           since,
                                           name,
                                           queue)) < 0)
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
    struct list_ctx *ctx = arg;
    job_list_error_t err;
    json_t *jobs;
    json_t *attrs;
    int max_entries;
    double since = 0.;
    uint32_t userid;
    int states;
    int results;
    const char *name = NULL;
    const char *queue = NULL;

    if (flux_request_unpack (msg, NULL, "{s:i s:o s:i s:i s:i s?F s?s s?s}",
                             "max_entries", &max_entries,
                             "attrs", &attrs,
                             "userid", &userid,
                             "states", &states,
                             "results", &results,
                             "since", &since,
                             "name", &name,
                             "queue", &queue) < 0) {
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
        states = (FLUX_JOB_STATE_PENDING
                  | FLUX_JOB_STATE_RUNNING
                  | FLUX_JOB_STATE_INACTIVE);

    /* If user sets no results, assume they want all information */
    if (!results)
        results = (FLUX_JOB_RESULT_COMPLETED
                   | FLUX_JOB_RESULT_FAILED
                   | FLUX_JOB_RESULT_CANCELED
                   | FLUX_JOB_RESULT_TIMEOUT);

    if (!(jobs = get_jobs (ctx->jsctx, &err, max_entries, since,
                           attrs, userid, states, results, name, queue)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void check_id_valid_continuation (flux_future_t *f, void *arg)
{
    struct idsync_data *isd = arg;
    struct job_state_ctx *jsctx = flux_future_aux_get (f, "job_state_ctx");

    assert (jsctx);

    if (flux_future_get (f, NULL) < 0) {
        if (flux_respond_error (jsctx->h, isd->msg, errno, NULL) < 0)
            flux_log_error (jsctx->h, "%s: flux_respond_error", __FUNCTION__);
        goto cleanup;
    }
    else {
        /* Job ID is legal.  Chance job-list has seen ID since this
         * lookup was done */
        struct job *job;
        if (!(job = zhashx_lookup (jsctx->index, &isd->id))
            || job->state == FLUX_JOB_STATE_NEW) {
            /* Must wait for job-list to see state change */
            if (idsync_wait_valid (jsctx->isctx, isd) < 0)
                flux_log_error (jsctx->h, "%s: idsync_wait_valid", __FUNCTION__);
            return;
        }
        else {
            json_t *o;
            if (!(o = get_job_by_id (jsctx, NULL, isd->msg,
                                     isd->id, isd->attrs, NULL))) {
                flux_log_error (jsctx->h, "%s: get_job_by_id", __FUNCTION__);
                goto cleanup;
            }
            if (flux_respond_pack (jsctx->h, isd->msg, "{s:O}", "job", o) < 0) {
                flux_log_error (jsctx->h, "%s: flux_respond_pack", __FUNCTION__);
                goto cleanup;
            }
        }
    }

cleanup:
    /* will free isd memory */
    idsync_check_id_valid_cleanup (jsctx->isctx, isd);
    return;
}

int check_id_valid (struct job_state_ctx *jsctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    json_t *attrs)
{
    struct idsync_data *isd = NULL;

    if (!(isd = idsync_check_id_valid (jsctx->isctx,
                                       id,
                                       msg,
                                       attrs))
        || flux_future_aux_set (isd->f_lookup,
                                "job_state_ctx",
                                jsctx,
                                NULL) < 0
        || flux_future_then (isd->f_lookup,
                             -1,
                             check_id_valid_continuation,
                             isd) < 0) {
        idsync_data_destroy (isd);
        return -1;
    }

    return 0;
}

/* Returns JSON object which the caller must free.  On error, return
 * NULL with errno set:
 *
 * EPROTO - malformed or empty id or attrs array
 * EINVAL - invalid id
 * ENOMEM - out of memory
 */
json_t *get_job_by_id (struct job_state_ctx *jsctx,
                       job_list_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       bool *stall)
{
    struct job *job;

    if (!(job = zhashx_lookup (jsctx->index, &id))) {
        if (stall) {
            if (check_id_valid (jsctx, msg, id, attrs) < 0) {
                flux_log_error (jsctx->h, "%s: check_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    if (job->state == FLUX_JOB_STATE_NEW) {
        if (stall) {
            /* Must wait for job-list to see state change */
            if (idsync_wait_valid_id (jsctx->isctx, id, msg, attrs) < 0) {
                flux_log_error (jsctx->h, "%s: idsync_wait_valid_id",
                                __FUNCTION__);
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
    struct list_ctx *ctx = arg;
    job_list_error_t err = {{0}};
    json_t *job;
    flux_jobid_t id;
    json_t *attrs = NULL;
    bool stall = false;

    if (flux_request_unpack (msg, NULL, "{s:I s?:O}",
                             "id", &id,
                             "attrs", &attrs) < 0) {
        seterror (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }

    if (attrs) {
        if (!json_is_array (attrs)) {
            seterror (&err, "invalid payload: attrs must be an array");
            errno = EPROTO;
            goto error;
        }
    }
    else {
        /* if user does not specify attrs, return all attrs */
        if (!(attrs = json_pack ("[s]", "all"))) {
            errno = ENOMEM;
            goto error;
        }
    }

    if (!(job = get_job_by_id (ctx->jsctx, &err, msg, id, attrs, &stall))) {
        /* response handled after KVS lookup complete */
        if (stall)
            goto stall;
        goto error;
    }

    if (flux_respond_pack (h, msg, "{s:O}", "job", job) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (job);
stall:
    json_decref (attrs);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (attrs);
}

static int list_attrs_append (json_t *a, const char *attr)
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
    const char **attrs;
    json_t *a;
    int i;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }

    attrs = job_attrs ();
    assert (attrs);

    for (i = 0; attrs[i] != NULL; i++) {
        if (list_attrs_append (a, attrs[i]) < 0)
            goto error;
    }

    if (list_attrs_append (a, "all") < 0)
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "attrs", a) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (a);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (a);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
