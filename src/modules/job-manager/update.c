/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* update - handle job update requests
 *
 * UPDATE REQUEST
 *
 * An update request payload consists of a jobid and dictionary of
 * period-delimited keys to update in that job, e.g.
 *
 * { "id": 123456, "updates": {"attributes.system.duration", 3600.}}
 *
 * OPERATION
 *
 * For each update key, a jobtap callback "job.update.KEY" is executed.
 * Currently at least one plugin MUST validate the update, therefore
 * update keys are only supported if there is a plugin that explicitly
 * allows the update by returning 0 from the 'job.update.*' callback.
 *
 * Note: in the future, some keys MAY be explicitly allowed in an allow
 * list directly within this module.
 *
 * If any update in a request fails to be validated, then the request
 * fails immediately. That is, either all updates are applied or none
 * are.
 *
 * Once all updates are validated by callbacks, then updates as applied
 * to jobspec are validated by passing an updated jobspec to the
 * `job.validate` jobtap plugin stack.
 *
 * Plugins may request that this validation step be skipped by setting
 * the 'validated' flag to 1 in the FLUX_PLUGIN_OUT_ARGS of the
 * `job.update.*` callback. The `job.validate` step will only be skipped
 * all keys in an update have the validated flag set.
 *
 * Plugins may also request a job feasibility check by setting a
 * 'feasibility' flag to 1 in the FLUX_PLUGIN_OUT_ARGS. If any plugin
 * requests a feasibility check, then feasibility is run for the proposed
 * jobspec as a whole.
 *
 * A plugin may request additional updates by setting an 'updates' key in
 * in the plugin out arguments. The updates key follows the same format as
 * the RFC 21 jobspec-update event and the update request defined here.
 *
 * As a special case, if a job is running and a duration update is
 * being applied, the update service will send a sched.expiration RPC to
 * the scheduler to ensure the expiration can be adjusted. If this RPC
 * fails with an error other than ENOSYS, then the update is rejected.
 *
 * If all steps above are successful, then a `jobspec-update`event is
 * posted for the job and a success response sent to the caller.
 *
 * If a job is running, and the update results in a change in `R`, then
 * a resource-update event MAY also be emitted for the job. Currently,
 * only an update of the expiration in R is supported.
 *
 * FUTURE WORK
 *
 * - Plugins should also somehow be able to initiate asynchronous work
 *   before validating an update. There is no support for async plugin
 *   callbacks in jobtap at this time, though.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <math.h>       /* INFINITY */
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libjob/idf58.h"

#include "update.h"
#include "job-manager.h"
#include "jobtap-internal.h"
#include "event.h"

struct update {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlistx_t *pending_requests;

    flux_future_t *kvs_watch_f;
    double instance_expiration;
};

struct update_request {
    void *handle;                 /* zlistx_t handle                       */
    flux_future_t *feasibility_f; /* feasibility request future            */
    flux_future_t *expiration_f;  /* sched.expiration request future       */
    struct update *update;        /* pointer back to update struct         */
    const flux_msg_t *msg;        /* original update request msg           */
    struct flux_msg_cred cred;    /* update request credentials            */
    struct job *job;              /* target job                            */
    json_t *updates;              /* requested updates object              */
    unsigned int validate:1;      /* 1: validate updates, 0: no validation */
};

static void update_request_destroy (struct update_request *req)
{
    if (req) {
        int saved_errno = errno;
        flux_future_destroy (req->feasibility_f);
        flux_future_destroy (req->expiration_f);
        flux_msg_decref (req->msg);
        job_decref (req->job);
        free (req);
        errno = saved_errno;
    }
}

/* zlistx_t destructor_fn */
static void update_request_destructor (void **item)
{
    if (item) {
        struct update_request *req = *item;
        update_request_destroy (req);
        *item = NULL;
    }
}

static struct update_request *
update_request_create (struct update *update,
                       const flux_msg_t *msg,
                       struct flux_msg_cred cred,
                       struct job *job,
                       json_t *updates)
{
    struct update_request *req;

    if (!(req = calloc (1, sizeof (*req))))
        return NULL;
    req->update = update;
    req->msg = flux_msg_incref (msg);
    req->job = job_incref (job);
    req->cred = cred;
    req->updates = updates;
    return req;
}

static int expiration_from_duration (struct job *job,
                                     flux_error_t *errp,
                                     double duration,
                                     double *expiration)
{
    if (duration == 0.)
        *expiration = 0.;
    else {
        /*  Decode starttime of job's current R and add updated duration:
        */
        double starttime = -1.0;
        if (!job->R_redacted
            || json_unpack (job->R_redacted,
                            "{s:{s?F}}",
                            "execution",
                            "starttime", &starttime) < 0
            || starttime <= 0.) {
            errprintf (errp, "unable to get job starttime");
            return -1;
        }
        *expiration = starttime + duration;
        if (*expiration <= flux_reactor_time ()) {
            errprintf (errp,
                       "requested duration places job expiration in the past");
            return -1;
        }
    }
    return 0;
}

static int post_resource_updates (struct job_manager *ctx,
                                  struct job *job,
                                  json_t *updates,
                                  flux_error_t *errp)
{
    double duration, expiration;

    /*  Updates for a running job may require a corresponding
     *  resource-update event. Currently this only applies to
     *  a duration update for a running job.
     */
    if (json_unpack (updates,
                     "{s:F}",
                     "attributes.system.duration", &duration) < 0)
        return 0;

    if (expiration_from_duration (job, errp, duration, &expiration) < 0) {
        return -1;
    }
    /*  Post resource-update event to modify expiration:
     */
    if (event_job_post_pack (ctx->event,
                             job,
                             "resource-update",
                             0,
                             "{s:f}",
                             "expiration", expiration) < 0) {
        errprintf (errp, "failed to pack resource-update event");
        return -1;
    }
    return 0;
}

static void post_job_updates (struct job_manager *ctx,
                              const flux_msg_t *msg,
                              struct flux_msg_cred cred,
                              struct job *job,
                              json_t *updates,
                              int validate)
{
    flux_error_t error;

    /*  If this update was requested by the instance owner, and the
     *  job owner is not the instance owner, and job validation was
     *  bypassed (validate != true), then disable future job updates
     *  as not permitted by marking the job immutable.
     *
     *  The reasons for doing this are two-fold:
     *
     *  - A future update of an unrelated attribute could fail validation
     *    due to this attribute update. This could result in a confusing
     *    error message.
     *
     *  - Bypassing validation for individual, previously updated attributes
     *    could be complex and might open the update process to unintended
     *    vulnerabilities (e.g. a user update after an instance owner update
     *    could allow a job access to resources, time limits, etc that are
     *    not intended for normal users.)
     */
    if (!validate
        && (cred.rolemask & FLUX_ROLE_OWNER)
        && cred.userid != job->userid) {
        if (event_job_post_pack (ctx->event,
                                job,
                                "set-flags",
                                0,
                                "{s:[s]}",
                                "flags", "immutable") < 0) {
            errprintf (&error, "failed to set job immutable flag");
            goto error;
        }

    }

    /*  All updates have been allowed by plugins and validated as a unit,
     *  so now emit jobspec-update event.
     */
    if (event_job_post_pack (ctx->event,
                             job,
                             "jobspec-update",
                             0,
                             "O",
                             updates) < 0) {
        errprintf (&error, "failed to pack jobspec-update event");
        goto error;
    }

    /*  If job is running, then post any necessary resource-update events:
     */
    if (job->state & FLUX_JOB_STATE_RUNNING
        && post_resource_updates (ctx, job, updates, &error) < 0) {
        goto error;
    }

    if (flux_respond (ctx->h, msg, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (ctx->h, msg, errno, error.text) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
}

static void feasibility_cb (flux_future_t *f, void *arg)
{
    struct update_request *req = arg;
    if (flux_future_get (f, NULL) < 0) {
        if (flux_respond_error (req->update->ctx->h,
                                req->msg,
                                errno,
                                future_strerror (f, errno)) < 0)
            flux_log_error (req->update->ctx->h,
                            "%s: flux_respond_error",
                            __FUNCTION__);
    }
    else {
        post_job_updates (req->update->ctx,
                          req->msg,
                          req->cred,
                          req->job,
                          req->updates,
                          req->validate);
    }
    zlistx_delete (req->update->pending_requests, req->handle);
}

static void sched_expiration_cb (flux_future_t *f, void *arg)
{
    struct update_request *req = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ENOSYS) {
        if (flux_respond_error (req->update->ctx->h,
                                req->msg,
                                errno,
                                "scheduler refused expiration update") < 0)
            flux_log_error (req->update->ctx->h,
                            "%s: flux_respond_error",
                            __FUNCTION__);
    }
    else {
        post_job_updates (req->update->ctx,
                          req->msg,
                          req->cred,
                          req->job,
                          req->updates,
                          req->validate);
    }
    zlistx_delete (req->update->pending_requests, req->handle);
}

static struct update_request *
pending_request_create (struct update *update,
                        const flux_msg_t *msg,
                        struct flux_msg_cred cred,
                        struct job *job,
                        json_t *updates,
                        int validate)
{
    struct update_request *req = NULL;
    if (!(req = update_request_create (update, msg, cred, job, updates))
        || !(req->handle = zlistx_add_end (update->pending_requests, req)))
        goto error;
    req->validate = validate;
    return req;
error:
    update_request_destroy (req);
    errno = ENOMEM;
    return NULL;
}

static int update_feasibility_check (struct update *update,
                                     const flux_msg_t *msg,
                                     struct flux_msg_cred cred,
                                     struct job *job,
                                     json_t *updates,
                                     int validate)
{
    json_t *jobspec = NULL;
    struct update_request *req = NULL;
    flux_future_t *f = NULL;

    if (!(jobspec = job_jobspec_with_updates (job, updates))
        || !(req = pending_request_create (update,
                                           msg,
                                           cred,
                                           job,
                                           updates,
                                           validate))
        || !(f = flux_rpc_pack (update->ctx->h,
                                "feasibility.check",
                                0,
                                0,
                                "{s:O}",
                                "jobspec", jobspec))
        || flux_future_then (f, -1., feasibility_cb, req) < 0)
        goto error;
    req->feasibility_f = f;
    json_decref (jobspec);
    return 0;
error:
    json_decref (jobspec);
    update_request_destroy (req);
    flux_future_destroy (f);
    return -1;

}

static int sched_expiration_check (struct update *update,
                                   flux_error_t *errp,
                                   const flux_msg_t *msg,
                                   struct flux_msg_cred cred,
                                   struct job *job,
                                   json_t *updates,
                                   int validate)
{
    struct update_request *req = NULL;
    flux_future_t *f = NULL;
    double expiration, duration;

    if (json_unpack (updates,
                     "{s:F}",
                     "attributes.system.duration", &duration) < 0) {
        errprintf (errp, "failed to unpack attributes.system.duration");
        return -1;
    }
    if (expiration_from_duration (job, errp, duration, &expiration) < 0)
        return -1;

    if (!(req = pending_request_create (update,
                                        msg,
                                        cred,
                                        job,
                                        updates,
                                        validate))
        || !(f = flux_rpc_pack (update->ctx->h,
                                "sched.expiration",
                                0,
                                0,
                                "{s:I s:f}",
                                "id", job->id,
                                "expiration", expiration))
        || flux_future_then (f, -1., sched_expiration_cb, req) < 0) {
        errprintf (errp,
                   "failed to send sched.expiration rpc: %s",
                   strerror (errno));
        goto error;
    }
    req->expiration_f = f;
    return 0;
error:
    update_request_destroy (req);
    flux_future_destroy (f);
    return -1;
}


static void update_job (struct update *update,
                        const flux_msg_t *msg,
                        struct flux_msg_cred cred,
                        struct job *job,
                        json_t *updates)
{
    const char *errmsg = NULL;
    flux_error_t error = {{0}};
    const char *key;
    json_t *value;
    int validate = 0; /* validation of result necessary */
    int feasibility = 0; /* feasibilty check necessary */
    json_t *additional_updates = NULL;
    struct job_manager *ctx = update->ctx;

    /*  Loop through one or more proposed updates in `updates` object
     *  and call `job.update.<key>` job plugin(s) to validate each
     *  update.
     */
    json_object_foreach (updates, key, value) {
        int needs_validation = 1;
        int require_feasibility = 0;
        if (jobtap_job_update (ctx->jobtap,
                               cred,
                               job,
                               key,
                               value,
                               &needs_validation,
                               &require_feasibility,
                               &additional_updates,
                               &error) < 0)
            goto error;
        /*  If any jobspec key needs further validation, then all
         *  keys will be validated at the same time. This means a key
         *  that might not need further validation when updated alone
         *  may need to be validated when paired with other keys in a
         *  single update:
         */
        if (needs_validation)
            validate = 1;
        /*  Similarly, if any key requires a feasibility check, then
         *  request feasibilty on the update as a whole.
         */
        if (require_feasibility)
            feasibility = 1;
    }
    if (additional_updates
        && json_object_update (updates, additional_updates) < 0) {
        errprintf (&error, "unable to apply additional required updates");
        goto error;
    }
    if (validate
        && jobtap_validate_updates (ctx->jobtap,
                                    job,
                                    updates,
                                    &error) < 0)
        goto error;

    if (feasibility)
        update_feasibility_check (update, msg, cred, job, updates, validate);
    else if (job->state & FLUX_JOB_STATE_RUNNING
             && json_object_get (updates, "attributes.system.duration")) {
        if (sched_expiration_check (update,
                                    &error,
                                    msg,
                                    cred,
                                    job,
                                    updates,
                                    validate) < 0) {
            goto error;
        }
    }
    else
        post_job_updates (ctx, msg, cred, job, updates, validate);

    json_decref (additional_updates);
    return;
error:
    if (strlen (error.text) != 0)
        errmsg = error.text;
    if (flux_respond_error (ctx->h, msg, EINVAL, errmsg) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (additional_updates);
}

static void update_handle_request (flux_t *h,
                                   flux_msg_handler_t *mh,
                                   const flux_msg_t *msg,
                                   void *arg)
{
    struct update *update = arg;
    struct job_manager *ctx = update->ctx;
    flux_jobid_t id;
    struct job *job;
    json_t *updates;
    struct flux_msg_cred cred;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:o}",
                             "id", &id,
                             "updates", &updates) < 0)
        goto error;

    /*  Validate updates object, currently all updates MUST
     *  start with `attributes.`:
     */
    if (!validate_jobspec_updates (updates)) {
        errstr = "one or more jobspec updates are invalid";
        errno = EINVAL;
        goto error;
    }
    /*  Verify jobid exists and is not inactive
     */
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (!(job = zhashx_lookup (ctx->inactive_jobs, &id))) {
            errstr = "unknown job id";
            errno = ENOENT;
        }
        else {
            errstr = "job is inactive";
            errno = EINVAL;
        }
        goto error;
    }
    /*  Fetch the credential from this message and ensure the user
     *  has authorization to update this job.
     */
    if (flux_msg_get_cred (msg, &cred) < 0
        || flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests may only update their own jobs";
        goto error;
    }
    if (job->immutable && !(cred.rolemask & FLUX_ROLE_OWNER)) {
        errstr = "job is immutable due to previous instance owner update";
        errno = EPERM;
        goto error;
    }
    /*  Process the update request. Response will be handled in update_job().
     */
    update_job (update, msg, cred, job, updates);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void send_error_responses (struct update *update)
{
    struct update_request *req = zlistx_first (update->pending_requests);
    while (req) {
        if (flux_respond_error (update->ctx->h,
                                req->msg,
                                EAGAIN,
                                "job manager is shutting down") < 0)
            flux_log_error (update->ctx->h,
                            "%s: error responding to "
                            "job-manager.update request",
                            __FUNCTION__);
        req = zlistx_next (update->pending_requests);
    }
}

static void update_expiration_from_lookup_response (struct update *update,
                                                    flux_future_t *f)
{
    flux_t *h = update->ctx->h;
    const char *R;
    json_t *o = NULL;
    json_error_t error;

    error.text[0] = '\0';

    if (flux_kvs_lookup_get (f, &R) < 0
        || !(o = json_loads (R, 0, NULL))
        || json_unpack_ex (o, &error, 0,
                           "{s:{s:F}}",
                           "execution",
                            "expiration", &update->instance_expiration) < 0)
        flux_log (h,
                  LOG_ERR,
                  "failed to unpack current instance expiration: %s",
                  error.text);
    json_decref (o);
}

static inline double expiration_diff (double old, double new)
{
    /*  If the old expiration was 0. (unlimited) then return -inf since
     *  this best represents the reduction of expiration from unlimited.
     *  If new expiration is unlimited, then return +inf.
     *  O/w, return difference between new and old.
     */
    if (old == 0.)
        return -INFINITY;
    if (new == 0.)
        return INFINITY;
    return new - old;
}

/*  An update to resource.R has occurred. Adjust expiration of all running
 *  jobs where no duration is set in jobspec, but the job currently has a set
 *  expiration. This implies the expiration was set automatically by the
 *  scheduler and needs an update.
 *
 *  The motivating case here is an administrative extension of a batch or
 *  alloc job time limit. This code extends that expiration update to all
 *  running jobs, which otherwise may have their expiration set to the
 *  previous instance time limit.
 */

static void resource_update_cb (flux_future_t *f, void *arg)
{
    struct update *update = arg;
    flux_t *h = update->ctx->h;
    struct job *job;
    double old_expiration = update->instance_expiration;

    update_expiration_from_lookup_response (update, f);
    flux_future_reset (f);

    /*  If this is the first successful update, or there are no
     *  running jobs, or the expiration was not updated, then there is
     *  nothing left to do.
     */
    if (old_expiration == -1.
        || fabs (update->instance_expiration - old_expiration) < 1.e-5
        || update->ctx->running_jobs == 0)
        return;

    flux_log (h,
              LOG_INFO,
              "resource expiration updated from %.2f to %.2f (%+.6g)",
              old_expiration,
              update->instance_expiration,
              expiration_diff (old_expiration, update->instance_expiration));


    /*  Otherwise, check each running job to determine if an adjustment
     *  of its expiration is required:
     */
    job = zhashx_first (update->ctx->active_jobs);
    while (job) {
        if (job->state == FLUX_JOB_STATE_RUN) {
            double expiration = -1.;
            double duration = 0.;
            /*
             *  Get current job expiration (if set) and jobspec duration.
             *  Assume the expiration job of the job needs to be updated
             *  only if and expiration was set for the job _and_ the job
             *  duration was unset or 0. This indicates that the expiration
             *  was likely automatically set by the scheduler based on
             *  the instance expiration (which is now being updated).
             *
             */
            if (json_unpack (job->R_redacted,
                            "{s:{s?F}}",
                            "execution",
                             "expiration", &expiration) < 0
                || json_unpack (job->jobspec_redacted,
                                "{s:{s:{s?F}}}",
                                "attributes",
                                 "system",
                                  "duration", &duration) < 0) {
                flux_log (h,
                          LOG_ERR,
                          "failed to unpack job %s data for expiration update",
                          idf58 (job->id));
            }
            /*  Job needs an update if no or unlimited duration specified
             *  in jobspec (duration == 0.) but an expiration was set in R
             *  (expiration >= 0.):
             */
            if (expiration >= 0 && duration == 0.) {
                flux_log (h,
                          LOG_INFO,
                          "updated expiration of %s from %.2f to %.2f (%+.6g)",
                          idf58 (job->id),
                          expiration,
                          update->instance_expiration,
                          expiration_diff (expiration,
                                           update->instance_expiration));
                if (event_job_post_pack (update->ctx->event,
                                         job,
                                         "resource-update",
                                         0,
                                         "{s:f}",
                                         "expiration",
                                         update->instance_expiration) < 0)
                    flux_log (h,
                              LOG_ERR,
                              "failed to pack resource-update event");
            }
        }
        job = zhashx_next (update->ctx->active_jobs);
    }
}

void update_ctx_destroy (struct update *update)
{
    if (update) {
        int saved_errno = errno;
        send_error_responses (update);
        flux_kvs_lookup_cancel (update->kvs_watch_f);
        flux_future_destroy (update->kvs_watch_f);
        flux_msg_handler_delvec (update->handlers);
        zlistx_destroy (&update->pending_requests);
        free (update);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.update",
        update_handle_request,
        FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct update *update_ctx_create (struct job_manager *ctx)
{
    struct update *update;

    if (!(update = calloc (1, sizeof (*update))))
        return NULL;
    update->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, update, &update->handlers) < 0)
        goto error;
    if (!(update->pending_requests = zlistx_new ())) {
        errno = ENOMEM;
        goto error;
    }
    zlistx_set_destructor (update->pending_requests,
                           update_request_destructor);

    /*  Watch resource.R in KVS for updates
     */
    update->kvs_watch_f = flux_kvs_lookup (ctx->h,
                                           NULL,
                                           FLUX_KVS_WATCH | FLUX_KVS_WAITCREATE,
                                           "resource.R");
    if (!update->kvs_watch_f
        || flux_future_then (update->kvs_watch_f,
                             -1.,
                             resource_update_cb,
                             update) < 0) {
        flux_log_error (ctx->h, "failed to setup watch on resource.R");
        goto error;
    }
    update->instance_expiration = -1.;
    return update;
error:
    update_ctx_destroy (update);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
