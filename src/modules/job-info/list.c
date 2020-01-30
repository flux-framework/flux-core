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

#include "list.h"
#include "job_state.h"

/* For a given job, create a JSON object containing the jobid and any
 * additional requested attributes and their values.  Returns JSON
 * object which the caller must free.  On error, return NULL with
 * errno set:
 *
 * EPROTO - malformed attrs array
 * ENOMEM - out of memory
 */
json_t *job_to_json (struct job *job, json_t *attrs)
{
    size_t index;
    json_t *value;
    json_t *o;
    json_t *val = NULL;

    if (!(o = json_object ()))
        goto error_nomem;
    if (!(val = json_integer (job->id)))
        goto error_nomem;
    if (json_object_set_new (o, "id", val) < 0) {
        json_decref (val);
        goto error_nomem;
    }
    json_array_foreach (attrs, index, value) {
        const char *attr = json_string_value (value);
        if (!attr) {
            errno = EINVAL;
            goto error;
        }
        if (!strcmp (attr, "userid")) {
            val = json_integer (job->userid);
        }
        else if (!strcmp (attr, "priority")) {
            val = json_integer (job->priority);
        }
        else if (!strcmp (attr, "t_submit")
                 || !strcmp (attr, "t_depend")) {
            if (!(job->states_mask & FLUX_JOB_DEPEND))
                continue;
            val = json_real (job->t_submit);
        }
        else if (!strcmp (attr, "t_sched")) {
            if (!(job->states_mask & FLUX_JOB_SCHED))
                continue;
            val = json_real (job->t_sched);
        }
        else if (!strcmp (attr, "t_run")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_real (job->t_run);
        }
        else if (!strcmp (attr, "t_cleanup")) {
            if (!(job->states_mask & FLUX_JOB_CLEANUP))
                continue;
            val = json_real (job->t_cleanup);
        }
        else if (!strcmp (attr, "t_inactive")) {
            if (!(job->states_mask & FLUX_JOB_INACTIVE))
                continue;
            val = json_real (job->t_inactive);
        }
        else if (!strcmp (attr, "state")) {
            val = json_integer (job->state);
        }
        else if (!strcmp (attr, "name")) {
            val = json_string (job->name);
        }
        else if (!strcmp (attr, "ntasks")) {
            val = json_integer (job->ntasks);
        }
        else if (!strcmp (attr, "nnodes")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_integer (job->nnodes);
        }
        else if (!strcmp (attr, "ranks")) {
            if (!(job->states_mask & FLUX_JOB_RUN))
                continue;
            val = json_string (job->ranks);
        }
        else {
            errno = EINVAL;
            goto error;
        }
        if (val == NULL)
            goto error_nomem;
        if (json_object_set_new (o, attr, val) < 0) {
            json_decref (val);
            goto error_nomem;
        }
    }
    return o;
 error_nomem:
    errno = ENOMEM;
 error:
    ERRNO_SAFE_WRAP (json_decref, o);
    return NULL;
}

/* Put jobs from list onto jobs array, breaking if max_entries has
 * been reached. Returns 1 if jobs array is full, 0 if continue, -1
 * one error with errno set:
 *
 * ENOMEM - out of memory
 */
int get_jobs_from_list (json_t *jobs,
                        zlistx_t *list,
                        int max_entries,
                        json_t *attrs,
                        uint32_t userid,
                        int states)
{
    struct job *job;

    job = zlistx_first (list);
    while (job) {
        if (job->state & states) {
            if (userid == FLUX_USERID_UNKNOWN || job->userid == userid) {
                json_t *o;
                if (!(o = job_to_json (job, attrs)))
                    return -1;
                if (json_array_append_new (jobs, o) < 0) {
                    json_decref (o);
                    errno = ENOMEM;
                    return -1;
                }
                if (json_array_size (jobs) == max_entries)
                    return 1;
            }
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
                  int max_entries,
                  json_t *attrs,
                  uint32_t userid,
                  int states)
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
                                       ctx->jsctx->pending,
                                       max_entries,
                                       attrs,
                                       userid,
                                       states)) < 0)
            goto error;
    }

    if (states & FLUX_JOB_RUNNING) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           ctx->jsctx->running,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states)) < 0)
                goto error;
        }
    }

    if (states & FLUX_JOB_INACTIVE) {
        if (!ret) {
            if ((ret = get_jobs_from_list (jobs,
                                           ctx->jsctx->inactive,
                                           max_entries,
                                           attrs,
                                           userid,
                                           states)) < 0)
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
    json_t *jobs = NULL;
    json_t *attrs;
    int max_entries;
    uint32_t userid;
    int states;

    if (flux_request_unpack (msg, NULL, "{s:i s:o s:i s:i}",
                             "max_entries", &max_entries,
                             "attrs", &attrs,
                             "userid", &userid,
                             "states", &states) < 0)
        goto error;

    if (max_entries < 0 || !json_is_array (attrs)) {
        errno = EPROTO;
        goto error;
    }

    /* If user sets no states, assume they want all information */
    if (!states)
        states = (FLUX_JOB_PENDING
                  | FLUX_JOB_RUNNING
                  | FLUX_JOB_INACTIVE);

    if (!(jobs = get_jobs (ctx, max_entries, attrs, userid, states)))
        goto error;

    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);

    json_decref (jobs);
    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (jobs);
}

void list_attrs_cb (flux_t *h, flux_msg_handler_t *mh,
                    const flux_msg_t *msg, void *arg)
{
    if (flux_respond_pack (h, msg, "{s:[s,s,s,s,s,s,s,s,s,s,s,s]}",
                           "attrs",
                           "userid",
                           "priority",
                           "t_submit",
                           "t_depend",
                           "t_sched",
                           "t_run",
                           "t_cleanup",
                           "t_inactive",
                           "state",
                           "name",
                           "ntasks",
                           "nnodes",
                           "ranks") < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }

    return;

error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
