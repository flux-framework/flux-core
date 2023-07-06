/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* urgency - adjust job urgency
 *
 * Purpose:
 *   Support flux job urgency command for adjusting job urgency
 *   after submission.  Guests can reduce their jobs' urgency, or increase
 *   up to the default urgency.
 *
 * Input:
 * - job id
 * - new urgency
 *
 * Output:
 * - old urgency
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "event.h"
#include "alloc.h"
#include "job-manager.h"

#include "urgency.h"

#define MAXOF(a,b)   ((a)>(b)?(a):(b))

void urgency_handle_request (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    struct job *job;
    int urgency, orig_urgency;
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i}",
                             "id", &id,
                             "urgency", &urgency) < 0
        || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (urgency < FLUX_JOB_URGENCY_MIN
        || urgency > FLUX_JOB_URGENCY_MAX) {
        errstr = "urgency value is out of range";
        errno = EINVAL;
        goto error;
    }
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (!(job = zhashx_lookup (ctx->inactive_jobs, &id)))
            errstr = "unknown job";
        else
            errstr = "job is inactive";
        errno = EINVAL;
        goto error;
    }
    /* Security: guests can only adjust jobs that they submitted.
     */
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only reprioritize their own jobs";
        goto error;
    }
    /* Security: guests can only reduce urgency, or increase up to default.
     */
    if (!(cred.rolemask & FLUX_ROLE_OWNER)
        && urgency > MAXOF (FLUX_JOB_URGENCY_DEFAULT, job->urgency)) {
        errstr = "guests can only adjust urgency <= default";
        errno = EPERM;
        goto error;
    }
    if (job->has_resources) {
        errstr = "urgency cannot be changed once resources are allocated";
        errno = EINVAL;
        goto error;
    }
    /* Post event: this will update job->urgency, which will then result
     *  in a call to recalculate priority, which reprioritize job if there
     *  was a priority change.
     */
    orig_urgency = job->urgency;
    if (event_job_post_pack (ctx->event,
                             job,
                             "urgency",
                             0,
                             "{s:I s:i}",
                             "userid", (json_int_t) cred.userid,
                             "urgency", urgency) < 0)
        goto error;
    if (flux_respond_pack (h, msg, "{s:i}", "old_urgency", orig_urgency) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
