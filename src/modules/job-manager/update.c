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
 * If all steps above are successful, then a `jobspec-update`event is
 * posted for the job and a success response sent to the caller.
 *
 * FUTURE WORK
 *
 * - Some job updates may require feasibility checks on the resulting
 *   jobspec. There should be a flag for a plugin to require that the
 *   result be passed to the scheduler feasibility RPC.
 *
 * - The above change will require some asynchronous handling be added
 *   to this service
 *
 * - Plugins should also somehow be able to initiate asynchronous work
 *   before validating an update. There is no support for async plugin
 *   callbacks in jobtap at this time, though.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "update.h"
#include "job-manager.h"
#include "jobtap-internal.h"
#include "event.h"

struct update {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

static void update_job (struct job_manager *ctx,
                       const flux_msg_t *msg,
                       struct flux_msg_cred cred,
                       struct job *job,
                       json_t *updates)
{
    const char *errstr = NULL;
    char *error = NULL;
    const char *key;
    json_t *value;
    int validate = 0; /* validation of result necessary */

    /*  Loop through one or more proposed updates in `updates` object
     *  and call `job.update.<key>` job plugin(s) to validate each
     *  update.
     */
    json_object_foreach (updates, key, value) {
        int needs_validation = 1;
        if (jobtap_job_update (ctx->jobtap,
                               cred,
                               job,
                               key,
                               value,
                               &needs_validation,
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
    }
    if (validate
        && jobtap_validate_updates (ctx->jobtap,
                                    job,
                                    updates,
                                    &error) < 0)
        goto error;


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
            errstr = "failed to set job immutable flag";
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
        errstr = "failed to pack jobspec-update event";
        goto error;
    }
    if (flux_respond (ctx->h, msg, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (ctx->h, msg, errno, error ? error : errstr) < 0)
        flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
    free (error);
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
    update_job (ctx, msg, cred, job, updates);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void update_ctx_destroy (struct update *update)
{
    if (update) {
        int saved_errno = errno;
        flux_msg_handler_delvec (update->handlers);
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
    return update;
error:
    update_ctx_destroy (update);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
