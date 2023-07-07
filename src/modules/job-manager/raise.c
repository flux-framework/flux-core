/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* raise - raise an exception on a job
 *
 * Purpose:
 *   Handle job-manager.raise RPC
 *
 * Input:
 * - job id, severity, type, note (optional)
 *
 * Action:
 * - publish exception event
 * - update kvs event log
 * - transition state to CLEANUP for severity=0
 *
 * Caveats:
 * - Exception event publishing is "open loop" (unlikely error not caught).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <unistd.h>
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"

#include "job.h"
#include "event.h"
#include "raise.h"
#include "job-manager.h"

struct raise {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

int raise_check_type (const char *s)
{
    const char *cp;

    if (strlen (s)  == 0)
        return -1;
    for (cp = s; *cp != '\0'; cp++)
        if (isspace (*cp) || *cp == '=')
            return -1;
    return 0;
}

int raise_check_severity (int severity)
{
    if (severity < 0 || severity > 7)
        return -1;
    return 0;
}

int raise_job_exception (struct job_manager *ctx,
                         struct job *job,
                         const char *type,
                         int severity,
                         uint32_t userid,
                         const char *note)
{
    flux_jobid_t id = job->id;
    flux_future_t *f;
    json_t *evctx;

    // create exception event context with the required keys per RFC 21
    if (!(evctx = json_pack ("{s:s s:i}", "type", type, "severity", severity)))
        goto nomem;
    // work around flux-framework/flux-core#5314
    if (!note)
        note = "";
    // add optional userid key
    if (userid != FLUX_USERID_UNKNOWN) {
        json_t *val;
        if (!(val = json_integer (userid))
            || json_object_set_new (evctx, "userid", val) < 0) {
            json_decref (val);
            goto nomem;
        }
    }
    // add optional note key
    if (note) {
        json_t *val;
        if (!(val = json_string (note))
            || json_object_set_new (evctx, "note", val) < 0) {
            json_decref (val);
            goto nomem;
        }
    }
    // post exception to job eventlog
    if (event_job_post_pack (ctx->event, job, "exception", 0, "O", evctx) < 0)
        goto error;
    // publish job-exception event
    if (!(f = flux_event_publish_pack (ctx->h,
                                       "job-exception",
                                       FLUX_MSGFLAG_PRIVATE,
                                       "{s:I s:s s:i}",
                                       "id", id,
                                       "type", type,
                                       "severity", severity)))
        goto error;
    flux_future_destroy (f);
    json_decref (evctx);
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, evctx);
    return -1;
}

void raise_handle_request (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    struct job *job;
    int severity;
    const char *type;
    const char *note = NULL;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i s:s s?s}",
                             "id", &id,
                             "severity", &severity,
                             "type", &type,
                             "note", &note) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (raise_check_severity (severity)) {
        errstr = "invalid exception severity";
        errno = EPROTO;
        goto error;
    }
    if (raise_check_type (type) < 0) {
        errstr = "invalid exception type";
        errno = EPROTO;
        goto error;
    }
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (!(job = zhashx_lookup (ctx->inactive_jobs, &id)))
            errstr = "unknown job id";
        else
            errstr = "job is inactive";
        errno = ENOENT;
        goto error;
    }
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only raise exceptions on their own jobs";
        goto error;
    }
    if (raise_job_exception (ctx, job, type, severity, cred.userid, note) < 0)
        goto error;
    /* NB: job object may be destroyed in event_job_post_pack().
     * Do not reference the object after this point:
     */
    job = NULL;
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Create a list of jobs matching userid, state_mask criteria.
 * FLUX_USERID_UNKNOWN is a wildcard that matches any user.
 */
int find_jobs (struct job_manager *ctx,
               uint32_t userid,
               int state_mask,
               zlistx_t **lp)
{
    zlistx_t *l;
    struct job *job;

    if (!(l = zlistx_new()))
        goto nomem;
    zlistx_set_destructor (l, job_destructor);
    zlistx_set_duplicator (l, job_duplicator);

    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (!(job->state & state_mask))
            goto next;
        if (userid != FLUX_USERID_UNKNOWN && userid != job->userid)
            goto next;
        if (!zlistx_add_end (l, job))
            goto nomem;
next:
        job = zhashx_next (ctx->active_jobs);
    }
    *lp = l;
    return 0;
nomem:
    zlistx_destroy (&l);
    errno = ENOMEM;
    return -1;
}

/* Raise exception on all jobs of 'userid' with state matching 'mask'.
 * Consider userid == FLUX_USERID_UNKNOWN to be a wildcard matching all users.
 */
void raiseall_handle_request (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct job_manager *ctx = arg;
    uint32_t userid;
    struct flux_msg_cred cred;
    int dry_run;
    int state_mask;
    int severity;
    const char *type;
    const char *note = NULL;
    const char *errstr = NULL;
    zlistx_t *target_jobs = NULL;
    struct job *job;
    int error_count = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s:i s:i s:i s:s s?s}",
                             "dry_run", &dry_run,
                             "userid", &userid,
                             "states", &state_mask,
                             "severity", &severity,
                             "type", &type,
                             "note", &note) < 0)
        goto error;
    if (flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    /* Only the instance owner gets to use the userid wildcard.
     * Guests must specify 'userid' = themselves.
     */
    if (flux_msg_cred_authorize (cred, userid) < 0) {
        errstr = "guests can only raise exceptions on their own jobs";
        goto error;
    }
    if (raise_check_severity (severity)) {
        errstr = "invalid exception severity";
        errno = EPROTO;
        goto error;
    }
    if (raise_check_type (type) < 0) {
        errstr = "invalid exception type";
        errno = EPROTO;
        goto error;
    }
    if (find_jobs (ctx, userid, state_mask, &target_jobs) < 0)
        goto error;
    if (!dry_run) {
        job = zlistx_first (target_jobs);
        while (job) {
            if (raise_job_exception (ctx,
                                     job,
                                     type,
                                     severity,
                                     cred.userid,
                                     note) < 0) {
                flux_log_error (h,
                                "error raising exception on id=%s",
                                idf58 (job->id));
                error_count++;
            }
            job = zlistx_next (target_jobs);
        }
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i}",
                           "count", zlistx_size (target_jobs),
                           "errors", error_count) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    zlistx_destroy (&target_jobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    zlistx_destroy (&target_jobs);
}

void raise_ctx_destroy (struct raise *raise)
{
    if (raise) {
        int saved_errno = errno;
        flux_msg_handler_delvec (raise->handlers);
        free (raise);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.raise",
        raise_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.raiseall",
        raiseall_handle_request,
        FLUX_ROLE_USER,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct raise *raise_ctx_create (struct job_manager *ctx)
{
    struct raise *raise;

    if (!(raise = calloc (1, sizeof (*raise))))
        return NULL;
    raise->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &raise->handlers) < 0)
        goto error;
    return raise;
error:
    raise_ctx_destroy (raise);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
