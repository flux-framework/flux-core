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
#include "queue.h"
#include "event.h"
#include "priority.h"

#define MAXOF(a,b)   ((a)>(b)?(a):(b))

void priority_handle_request (flux_t *h, struct queue *queue,
                              struct event_ctx *event_ctx,
                              const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    int priority;
    int rc;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "priority", &priority) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (priority < FLUX_JOB_PRIORITY_MIN || priority > FLUX_JOB_PRIORITY_MAX) {
        errstr = "priority value is out of range";
        errno = EINVAL;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id))) {
        errstr = "unknown job";
        goto error;
    }
    /* Security: guests can only adjust jobs that they submitted.
     */
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job->userid) {
        errstr = "guests can only reprioritize their own jobs";
        errno = EPERM;
        goto error;
    }
    /* Security: guests can only reduce priority, or increase up to default.
     */
    if (!(rolemask & FLUX_ROLE_OWNER)
            && priority > MAXOF (FLUX_JOB_PRIORITY_DEFAULT, job->priority)) {
        errstr = "guests can only adjust priority <= default";
        errno = EPERM;
        goto error;
    }
    /* Post event, change job's queue position, and respond.
     */
    if (event_job_post_pack (event_ctx, job,
                             "priority",
                             "{ s:i s:i }",
                             "userid", userid,
                             "priority", priority) < 0)
        goto error;
    queue_reorder (queue, job, job->queue_handle);
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (errstr)
        rc = flux_respond_error (h, msg, errno, "%s", errstr);
    else
        rc = flux_respond_error (h, msg, errno, NULL);
    if (rc < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
