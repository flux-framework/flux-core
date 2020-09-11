/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* priority - adjust job priority
 *
 * Purpose:
 *   Support flux job priority command for adjusting job priority
 *   after submission.  Guests can reduce their jobs' priority, or increase
 *   up to the default priority.
 *
 * Input:
 * - job id
 * - new priority
 *
 * Output:
 * - n/a
 *
 * Caveats:
 * - Need to handle case where job has already made request for resources.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "job.h"
#include "event.h"
#include "alloc.h"
#include "job-manager.h"

#include "priority.h"

#define MAXOF(a,b)   ((a)>(b)?(a):(b))

void priority_handle_request (flux_t *h,
                              flux_msg_handler_t *mh,
                              const flux_msg_t *msg,
                              void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    struct job *job;
    int priority, orig_priority;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "priority", &priority) < 0
                    || flux_msg_get_cred (msg, &cred) < 0)
        goto error;
    if (priority < FLUX_JOB_PRIORITY_MIN || priority > FLUX_JOB_PRIORITY_MAX) {
        errstr = "priority value is out of range";
        errno = EINVAL;
        goto error;
    }
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        errstr = "unknown job";
        errno = EINVAL;
        goto error;
    }
    /* Security: guests can only adjust jobs that they submitted.
     */
    if (flux_msg_cred_authorize (cred, job->userid) < 0) {
        errstr = "guests can only reprioritize their own jobs";
        goto error;
    }
    /* Security: guests can only reduce priority, or increase up to default.
     */
    if (!(cred.rolemask & FLUX_ROLE_OWNER)
            && priority > MAXOF (FLUX_JOB_PRIORITY_DEFAULT, job->priority)) {
        errstr = "guests can only adjust priority <= default";
        errno = EPERM;
        goto error;
    }
    /* RFC 27 does not yet handle priority changes after alloc request
     * has been sent to the scheduler.  Also, alloc_queue_reorder() will
     * segfault if job->handle is NULL, which is the case if the job is
     * no longer in alloc->queue.
     */
    if (job->alloc_pending) {
        errstr = "job has made an alloc request to scheduler, "
                 "priority cannot be changed";
        errno = EINVAL;
        goto error;
    }
    if (job->has_resources) {
        errstr = "priority cannot be changed once resources are allocated";
        errno = EINVAL;
        goto error;
    }
    /* Post event, change job's queue position, and respond.
     */
    orig_priority = job->priority;
    if (event_job_post_pack (ctx->event, job,
                             "priority",
                             "{ s:i s:i }",
                             "userid", cred.userid,
                             "priority", priority) < 0)
        goto error;
    if (priority != orig_priority)
        alloc_queue_reorder (ctx->alloc, job);
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
