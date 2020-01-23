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
 * - publish exception event (for e.g. scheduler to abort queued requests)
 * - update kvs event log
 * - transition state to CLEANUP for severity=0
 *
 * Caveats:
 * - Exception event publishing is "open loop" (unlikely error not caught).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <flux/core.h>

#include "job.h"
#include "event.h"
#include "raise.h"
#include "job-manager.h"

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
    flux_future_t *f;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:i s:s s?:s}",
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
        errstr = "unknown job id";
        errno = EINVAL;
        goto error;
    }
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only raise exceptions on their own jobs";
        goto error;
    }
    if (event_job_post_pack (ctx->event, job,
                             "exception",
                             "{ s:s s:i s:i s:s }",
                             "type", type,
                             "severity", severity,
                             "userid", cred.userid,
                             "note", note ? note : "") < 0)
        goto error;
    /* NB: job object may be destroyed in event_job_post_pack().
     * Do not reference the object after this point:
     */
    job = NULL;
    if (!(f = flux_event_publish_pack (h, "job-exception",
                                       FLUX_MSGFLAG_PRIVATE,
                                       "{s:I s:s s:i}",
                                       "id", id,
                                       "type", type,
                                       "severity", severity)))
        goto error;
    flux_future_destroy (f);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
