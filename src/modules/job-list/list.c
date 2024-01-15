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
#include "src/common/libutil/errprintf.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "job-list.h"
#include "idsync.h"
#include "list.h"
#include "job_util.h"
#include "job_data.h"
#include "match.h"
#include "state_match.h"

json_t *get_job_by_id (struct job_state_ctx *jsctx,
                       flux_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       flux_job_state_t state,
                       bool *stall);

/* Put jobs from list onto jobs array, breaking if max_entries has
 * been reached. Returns 1 if jobs array is full, 0 if continue, -1
 * one error with errno set:
 *
 * ENOMEM - out of memory
 */
int get_jobs_from_list (json_t *jobs,
                        flux_error_t *errp,
                        zlistx_t *list,
                        int max_entries,
                        json_t *attrs,
                        double since,
                        struct list_constraint *c)
{
    struct job *job;

    job = zlistx_first (list);
    while (job) {
        int ret;

        /*  If job->t_inactive > 0. we're on the inactive jobs list and jobs are
         *  sorted on the inactive list, larger t_inactive first.
         *
         *  If job->t_inactive > since, this is a job that could potentially be returned
         *
         *  So if job->t_inactive <= since, then we're done b/c the rest of the inactive
         *   jobs cannot be returned.
         */
        if (job->t_inactive > 0. && job->t_inactive <= since)
            break;

        if ((ret = job_match (job, c, errp)) < 0)
            return -1;
        if (ret) {
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
                  flux_error_t *errp,
                  int max_entries,
                  double since,
                  json_t *attrs,
                  struct list_constraint *c,
                  struct state_constraint *statec)
{
    json_t *jobs = NULL;
    int saved_errno;
    int ret = 0;

    if (!(jobs = json_array ()))
        goto error_nomem;

    /* We return jobs in the following order, pending, running,
     * inactive */

    if (state_match (FLUX_JOB_STATE_PENDING, statec)) {
        if ((ret = get_jobs_from_list (jobs,
                                       errp,
                                       jsctx->pending,
                                       max_entries,
                                       attrs,
                                       0.,
                                       c)) < 0)
            goto error;
    }

    if (state_match (FLUX_JOB_STATE_RUNNING, statec)) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           jsctx->running,
                                           max_entries,
                                           attrs,
                                           0.,
                                           c)) < 0)
                goto error;
        }
    }

    if (state_match (FLUX_JOB_STATE_INACTIVE, statec)) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           errp,
                                           jsctx->inactive,
                                           max_entries,
                                           attrs,
                                           since,
                                           c)) < 0)
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

static int legacy_list_rpc (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            struct list_ctx *ctx,
                            int *max_entries,
                            json_t **attrs,
                            double *since,
                            json_t **legacy_constraint)
{
    uint32_t userid;
    int states;
    int results;
    const char *name = NULL;
    const char *queue = NULL;
    json_t *a = NULL;
    json_t *o;

    if (flux_request_unpack (msg, NULL, "{s:i s:o s:i s:i s:i s?F s?s s?s}",
                             "max_entries", max_entries,
                             "attrs", attrs,
                             "userid", &userid,
                             "states", &states,
                             "results", &results,
                             "since", since,
                             "name", &name,
                             "queue", &queue) < 0)
        return -1;

    /* Create constraint object given legacy inputs */

    if (!(a = json_array ()))
        goto error;

    if (userid != FLUX_USERID_UNKNOWN) {
        o = json_pack ("{s:[i]}", "userid", userid);
        if (!o || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }

    if (name) {
        o = json_pack ("{s:[s]}", "name", name);
        if (!o || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }

    if (queue) {
        o = json_pack ("{s:[s]}", "queue", queue);
        if (!o || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }

    /* N.B. in older code, if states == 0, then set states=<all the states>.
     * The equivalent in constraints is to not set a constraint. */
    if (states) {
        o = json_pack ("{s:[i]}", "states", states);
        if (!o || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }

    /* N.B. in older code, if results == 0, then set states=<all the results>.
     * The equivalent in constraints is to not set a constraint. */
    if (results) {
        o = json_pack ("{s:[i]}", "results", results);
        if (!o || json_array_append_new (a, o) < 0) {
            json_decref (o);
            goto error;
        }
    }

    if (!((*legacy_constraint) = json_pack ("{s:o}", "and", a)))
        goto error;

    return 0;

error:
    json_decref (a);
    return -1;
}

void list_cb (flux_t *h, flux_msg_handler_t *mh,
              const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    flux_error_t err;
    json_t *jobs;
    json_t *attrs;
    int max_entries;
    double since = 0.;
    json_t *constraint = NULL;
    json_t *legacy_constraint = NULL;
    struct list_constraint *c = NULL;
    struct state_constraint *statec = NULL;
    flux_error_t error;

    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
    if (flux_request_unpack (msg, NULL, "{s:i s:o s?F s?o}",
                             "max_entries", &max_entries,
                             "attrs", &attrs,
                             "since", &since,
                             "constraint", &constraint) < 0) {
        errprintf (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }
    if (!constraint) {
        /* Double check for legacy RPC fields since "constraint"
         * object is optional in new protocol. */
        int tmp_max_entries;
        json_t *tmp_attrs;
        double tmp_since = 0.;
        if (!legacy_list_rpc (h,
                              mh,
                              msg,
                              ctx,
                              &tmp_max_entries,
                              &tmp_attrs,
                              &tmp_since,
                              &legacy_constraint)) {
            max_entries = tmp_max_entries;
            attrs = tmp_attrs;
            since = tmp_since;
            constraint = legacy_constraint;
        }
    }
    if (max_entries < 0) {
        errprintf (&err, "invalid payload: max_entries < 0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (since < 0.) {
        errprintf (&err, "invalid payload: since < 0.0 not allowed");
        errno = EPROTO;
        goto error;
    }
    if (!json_is_array (attrs)) {
        errprintf (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }
    if (!(c = list_constraint_create (ctx->mctx, constraint, &error))) {
        errprintf (&err,
                   "invalid payload: constraint object invalid: %s",
                   error.text);
        errno = EPROTO;
        goto error;
    }
    if (!(statec = state_constraint_create (constraint, &error))) {
        errprintf (&err,
                   "invalid payload: constraint object invalid: %s",
                   error.text);
        errno = EPROTO;
        goto error;
    }

    if (!(jobs = get_jobs (ctx->jsctx, &err, max_entries, since,
                           attrs, c, statec)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (jobs);
    list_constraint_destroy (c);
    state_constraint_destroy (statec);
    json_decref (legacy_constraint);
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    list_constraint_destroy (c);
    state_constraint_destroy (statec);
    json_decref (legacy_constraint);
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
            if (idsync_wait_valid (jsctx->ctx->isctx, isd) < 0)
                flux_log_error (jsctx->h, "%s: idsync_wait_valid", __FUNCTION__);
            return;
        }
        else {
            json_t *o;
            if (!(o = get_job_by_id (jsctx, NULL, isd->msg,
                                     isd->id, isd->attrs, isd->state, NULL))) {
                flux_log_error (jsctx->h, "%s: get_job_by_id", __FUNCTION__);
                goto cleanup;
            }
            if (flux_respond_pack (jsctx->h, isd->msg, "{s:O}", "job", o) < 0) {
                json_decref (o);
                flux_log_error (jsctx->h, "%s: flux_respond_pack", __FUNCTION__);
                goto cleanup;
            }
            json_decref (o);
        }
    }

cleanup:
    /* will free isd memory */
    idsync_check_id_valid_cleanup (jsctx->ctx->isctx, isd);
    return;
}

int check_id_valid (struct job_state_ctx *jsctx,
                    const flux_msg_t *msg,
                    flux_jobid_t id,
                    json_t *attrs,
                    flux_job_state_t state)
{
    struct idsync_data *isd = NULL;

    if (!(isd = idsync_check_id_valid (jsctx->ctx->isctx,
                                       id,
                                       msg,
                                       attrs,
                                       state))
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
                       flux_error_t *errp,
                       const flux_msg_t *msg,
                       flux_jobid_t id,
                       json_t *attrs,
                       flux_job_state_t state,
                       bool *stall)
{
    struct job *job;

    if (!(job = zhashx_lookup (jsctx->index, &id))) {
        if (stall) {
            if (check_id_valid (jsctx, msg, id, attrs, state) < 0) {
                flux_log_error (jsctx->h, "%s: check_id_valid", __FUNCTION__);
                return NULL;
            }
            (*stall) = true;
        }
        return NULL;
    }

    /*  Always return job in inactive state, even if a requested state was
     *  provided. This avoids no response when a job does not enter a given
     *  state before becoming inactive, e.g. when a pending job is canceled.
     */
    if (job->state == FLUX_JOB_STATE_INACTIVE)
        return job_to_json (job, attrs, errp);

    /*  Otherwise, wait for given state if the job is still NEW or a specific
     *  state was requested that has not yet in the job states mask:
     */
    if ((state && !(job->states_mask & state))
        || job->state == FLUX_JOB_STATE_NEW) {
        if (stall) {
            /* Must wait for job-list to see state change */
            if (idsync_wait_valid_id (jsctx->ctx->isctx, id, msg, attrs, state) < 0) {
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
    flux_error_t err = {{0}};
    json_t *job;
    flux_jobid_t id;
    json_t *attrs;
    int state = 0;
    int valid_states = FLUX_JOB_STATE_ACTIVE | FLUX_JOB_STATE_INACTIVE;
    bool stall = false;

    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
    if (flux_request_unpack (msg, NULL, "{s:I s:o s?i}",
                             "id", &id,
                             "attrs", &attrs,
                             "state", &state) < 0) {
        errprintf (&err, "invalid payload: %s", flux_msg_last_error (msg));
        errno = EPROTO;
        goto error;
    }

    if (!json_is_array (attrs)) {
        errprintf (&err, "invalid payload: attrs must be an array");
        errno = EPROTO;
        goto error;
    }

    if (state && (state & ~valid_states)) {
        errprintf (&err, "invalid payload: invalid state specified");
        errno = EPROTO;
        goto error;
    }

    if (!(job = get_job_by_id (ctx->jsctx,
                               &err,
                               msg,
                               id,
                               attrs,
                               state,
                               &stall))) {
        /* response handled after KVS lookup complete */
        if (stall)
            goto stall;
        goto error;
    }

    if (flux_respond_pack (h, msg, "{s:O}", "job", job) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (job);
stall:
    return;

error:
    if (flux_respond_error (h, msg, errno, err.text) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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
    struct list_ctx *ctx = arg;
    const char **attrs;
    json_t *a = NULL;
    int i;

    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
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
