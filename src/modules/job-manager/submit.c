/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* submit.c - handle job-manager.submit request from job-ingest */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/errno_safe.h"

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "wait.h"
#include "jobtap-internal.h"

#include "submit.h"

struct submit {
    struct job_manager *ctx;
    bool submit_disable;
    char *disable_errmsg;
    flux_msg_handler_t **handlers;
};

static struct job *submit_unpack_job (json_t *o)
{
    struct job *job;

    if (!(job = job_create ()))
        return NULL;
    if (json_unpack (o, "{s:I s:i s:i s:f s:i s:O}",
                        "id", &job->id,
                        "urgency", &job->urgency,
                        "userid", &job->userid,
                        "t_submit", &job->t_submit,
                        "flags", &job->flags,
                        "jobspec", &job->jobspec_redacted) < 0) {
        errno = EPROTO;
        job_decref (job);
        job = NULL;
    }
    return job;
}

zlistx_t *submit_jobs_to_list (json_t *jobs)
{
    int saved_errno;
    size_t index;
    json_t *el;
    zlistx_t *newjobs;
    struct job * job;

    if (!(newjobs = zlistx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zlistx_set_destructor (newjobs, job_destructor);
    json_array_foreach (jobs, index, el) {
        if (!(job = submit_unpack_job (el)))
            goto error;
        if (zlistx_add_end (newjobs, job) < 0) {
            job_decref (job);
            errno = ENOMEM;
            goto error;
        }
    }
    return newjobs;
error:
    saved_errno = errno;
    zlistx_destroy (&newjobs);
    errno = saved_errno;
    return NULL;
}

int submit_hash_jobs (zhashx_t *active_jobs,
                      zlistx_t *newjobs)
{
    struct job * job = zlistx_first (newjobs);
    while (job) {
        if (zhashx_insert (active_jobs, &job->id, job) < 0) {
            /* zhashx_insert() fails if hash item already exists.
             * This is not an error - there is a window for restart_from_kvs()
             * to pick up a job that also has a submit request in flight.
             */
            if (zlistx_delete (newjobs, zlistx_cursor (newjobs)) < 0)
                return -1;
        }
        job = zlistx_next (newjobs);
    }
    return 0;
}

/* The submit request has failed.  Dequeue jobs recorded in 'newjobs',
 * then destroy the newjobs list.
 */
void submit_add_jobs_cleanup (zhashx_t *active_jobs, zlistx_t *newjobs)
{
    if (newjobs) {
        int saved_errno = errno;
        struct job *job = zlistx_first (newjobs);
        while (job) {
            zhashx_delete (active_jobs, &job->id);
            job = zlistx_next (newjobs);
        }
        zlistx_destroy (&newjobs);
        errno = saved_errno;
    }
}

/* Submit event requires special handling to use job->t_submit (from
 * job-ingest) as the event timestamp.
 */
static int submit_post_event (struct job_manager *ctx, struct job *job)
{
    json_t *entry = NULL;
    int rv = -1;

    entry = eventlog_entry_pack (job->t_submit,
                                 "submit",
                                 "{ s:i s:i s:i }",
                                 "userid", job->userid,
                                 "urgency", job->urgency,
                                 "flags", job->flags);
    if (!entry)
        return -1;

    rv = event_job_post_entry (ctx->event, job, "submit", 0, entry);
    ERRNO_SAFE_WRAP (json_decref, entry);
    return rv;
}

/*  Call the jobtap job.validate hook for all jobs in newjobs.
 *  If a plugin returns < 0 for a job, remove that job from the newjobs
 *   list and append the error to the errp array.
 */
static int submit_validate_jobs (struct job_manager *ctx,
                                 zlistx_t *newjobs,
                                 json_t **errp)
{
    struct job * job;
    json_t *array;

    if (!(array = json_array ())) {
        errno = ENOMEM;
        return -1;
    }
    job = zlistx_first (newjobs);
    while (job) {
        char *errmsg = NULL;
        json_t *entry = NULL;

        if (jobtap_validate (ctx->jobtap, job, &errmsg) < 0
            || jobtap_check_dependencies (ctx->jobtap,
                                          job,
                                          false,
                                          &errmsg) < 0) {
            /*
             *  This job is rejected: append error to error payload and
             *   delete the job from newjobs list
             */
            if (errmsg) {
                entry = json_pack ("[Is]", job->id, errmsg);
                free (errmsg);
            }
            else
                entry = json_pack ("[Is]", job->id, "rejected by plugin");

            /*  Append error to errors array and remove the job from
             *   the newjobs list since it failed validation.
             */
            if (entry == NULL
                || json_array_append_new (array, entry) < 0
                || zlistx_delete (newjobs, zlistx_cursor (newjobs)) < 0) {
                flux_log (ctx->h, LOG_ERR,
                          "submit_validate_jobs: failed to invalidate job");
                json_decref (entry);
                goto error;
            }
        }
        else {
            /*  The job has been accepted and will progress past the NEW
             *   state after it has been added to the active jobs hash.
             *
             *   Immediately notify any plugins of a new job here (unless
             *   the job is already hashed, an allowed condition) so that
             *   any internal plugin state (e.g. user job count) can be
             *   updated before the next job is validated.
             */
            if (!zhashx_lookup (ctx->active_jobs, &job->id))
                (void) jobtap_call (ctx->jobtap, job, "job.new", NULL);
        }
        job = zlistx_next (newjobs);
    }
    *errp = array;
    return 0;
error:
    json_decref (array);
    return -1;
}

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    json_t *jobs;
    json_t *errors = NULL;
    zlistx_t *newjobs = NULL;
    struct job *job;
    flux_msg_t *response = NULL;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (ctx->submit->submit_disable) {
        errno = EINVAL;
        errmsg = ctx->submit->disable_errmsg;
        goto error;
    }
    if (!(newjobs = submit_jobs_to_list (jobs))) {
        flux_log_error (h, "%s: error creating newjobs list", __FUNCTION__);
        goto error;
    }
    if (submit_validate_jobs (ctx, newjobs, &errors) < 0) {
        flux_log_error (h, "%s: error validating batch", __FUNCTION__);
        goto error;
    }
    if (submit_hash_jobs (ctx->active_jobs, newjobs) < 0) {
        flux_log_error (h, "%s: error enqueuing batch", __FUNCTION__);
        goto error;
    }
    /* Walk the list of new jobs and post submit event.
     * Side effects: update ctx->max_jobid and maintain count of waitables.
     */
    job = zlistx_first (newjobs);
    while (job) {
        if (submit_post_event (ctx, job) < 0) {
            flux_log_error (h, "error posting submit event for id=%ju",
                            (uintmax_t)job->id);
            goto error;
        }
        if ((job->flags & FLUX_JOB_WAITABLE))
            wait_notify_active (ctx->wait, job);
        if (ctx->max_jobid < job->id)
            ctx->max_jobid = job->id;

        job = zlistx_next (newjobs);
    }
    /* Attach response to commit batch, to maintain the invariant that the
     * job ID is only returned to the user after the submit event is committed.
     */
    if (!(response = flux_response_derive (msg, 0))
        || flux_msg_pack (response, "{s:O}", "errors", errors) < 0
        || event_batch_respond (ctx->event, response) < 0) {
        flux_log_error (h, "error enqueuing response to submit request");
        goto error;
    }
    flux_msg_decref (response);
    json_decref (errors);
    zlistx_destroy (&newjobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    flux_msg_decref (response);
    json_decref (errors);
    zlistx_destroy (&newjobs);
}

static void submit_admin_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    const char *error_prefix = "job submission is disabled: ";
    const char *errmsg = NULL;
    int enable;
    int query_only;
    const char *reason;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s:b s:s}",
                             "query_only",
                             &query_only,
                             "enable",
                             &enable,
                             "reason",
                             &reason) < 0)
        goto error;
    if (!query_only) {
        if (flux_msg_authorize (msg, FLUX_USERID_UNKNOWN) < 0) {
            errmsg = "Request requires owner credentials";
            goto error;
        }
        if (!enable) {
            char *errmsg;
            if (asprintf (&errmsg, "%s%s", error_prefix, reason) < 0)
                goto error;
            free (ctx->submit->disable_errmsg);
            ctx->submit->disable_errmsg = errmsg;
        }
        ctx->submit->submit_disable = enable ? false : true;
    }
    if (ctx->submit->submit_disable)
        reason = ctx->submit->disable_errmsg + strlen (error_prefix);
    if (flux_respond_pack (h,
                           msg,
                           "{s:b s:s}",
                           "enable",
                           ctx->submit->submit_disable ? 0 : 1,
                           "reason",
                           ctx->submit->submit_disable ? reason : "") < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void submit_ctx_destroy (struct submit *submit)
{
    if (submit) {
        int saved_errno = errno;
        flux_msg_handler_delvec (submit->handlers);
        free (submit->disable_errmsg);
        free (submit);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {   FLUX_MSGTYPE_REQUEST,
        "job-manager.submit",
        submit_cb,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.submit-admin",
        submit_admin_cb,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct submit *submit_ctx_create (struct job_manager *ctx)
{
    struct submit *submit;

    if (!(submit = calloc (1, sizeof (*submit))))
        return NULL;
    submit->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &submit->handlers) < 0)
        goto error;
    return submit;
error:
    submit_ctx_destroy (submit);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
