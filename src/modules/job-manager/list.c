/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* list - list jobs
 *
 * Purpose:
 *   Support queue lister tool like "flux queue" or queue watcher tool like
 *   "flux top" to obtain the jobs at head of queue with low overhead.
 *   Allow the set of job attributes returned to be customized, for
 *   customizable tool output.
 *
 *   The entire queue can be dumped if desired.  This is useful for testing
 *   job-manager queue management.
 *
 * Input:
 * - set of attributes to list per job
 * - max number of jobs to return from head of queue
 *
 * Output:
 * - array of job objects (job objects contain the requested attributes
 *   and their values)
 *
 * Caveats:
 * - Always returns one response message regardless of number of jobs.
 * - Only a hardwired list of attributes is supported.
 * - No limits on guest access.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"

#include "job.h"
#include "queue.h"
#include "list.h"
#include "job-manager.h"

/* Create a JSON array of 'job' objects, representing the head of the queue.
 * 'max_entries' determines the max number of jobs to return, 0=unlimited.
 * Returns JSON object which the caller must free.  On error, return NULL
 * with errno set:
 *
 * EPROTO - malformed or empty attrs array, max_entries out of range
 * ENOMEM - out of memory
 */
json_t *list_job_array (struct queue *queue, int max_entries)
{
    json_t *jobs = NULL;
    struct job *job;
    int saved_errno;

    if (max_entries < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(jobs = json_array ()))
        goto error_nomem;
    job = queue_first (queue);
    while (job) {
        json_t *o;
        if (!(o = json_pack ("{s:I s:i s:i s:f s:i}",
                             "id",
                             job->id,
                             "userid",
                             job->userid,
                             "priority",
                             job->priority,
                             "t_submit",
                             job->t_submit,
                             "state",
                             job->state)))
            goto error;
        if (json_array_append_new (jobs, o) < 0) {
            json_decref (o);
            goto error_nomem;
        }
        if (json_array_size (jobs) == max_entries)
            break;
        job = queue_next (queue);
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

void list_handle_request (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_manager *ctx = arg;
    int max_entries;
    json_t *jobs;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i}",
                             "max_entries",
                             &max_entries) < 0)
        goto error;
    if (!(jobs = list_job_array (ctx->queue, max_entries)))
        goto error;
    if (flux_respond_pack (h, msg, "{s:O}", "jobs", jobs) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    json_decref (jobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
