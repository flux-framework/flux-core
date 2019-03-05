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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "alloc.h"
#include "event.h"

#include "submit.h"

/* Enqueue jobs from 'jobs' array in queue.
 * On success, return a list of struct job's.
 * On failure, return NULL with errno set (no jobs enqueued).
 */
static zlist_t *enqueue_jobs (struct queue *queue, json_t *jobs)
{
    size_t index;
    json_t *el;
    zlist_t *newjobs;
    struct job *job;
    int saved_errno;

    if (!(newjobs = zlist_new ()))
        goto error;
    json_array_foreach (jobs, index, el) {
        if (!(job = job_create ()))
            goto error;
        if (json_unpack (el, "{s:I s:i s:i s:f s:i}",
                             "id", &job->id,
                             "priority", &job->priority,
                             "userid", &job->userid,
                             "t_submit", &job->t_submit,
                             "flags", &job->flags) < 0) {
            job_decref (job);
            goto error;
        }
        if (queue_insert (queue, job, &job->queue_handle) < 0) {
            job_decref (job);
            if (errno == EEXIST)
                continue; // don't report error - might be a race with restart
            goto error;
        }
        if (zlist_push (newjobs, job) < 0) {
            queue_delete (queue, job, job->queue_handle);
            job_decref (job);
            errno = ENOMEM;
            goto error;
        }
    }
    return newjobs;
error:
    saved_errno = errno;
    while ((job = zlist_pop (newjobs))) {
        queue_delete (queue, job, job->queue_handle);
        job_decref (job);
    }
    zlist_destroy (&newjobs);
    errno = saved_errno;
    return NULL;
}

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
void submit_handle_request (flux_t *h,
                            struct queue *queue,
                            struct alloc_ctx *alloc_ctx,
                            const flux_msg_t *msg)
{
    json_t *jobs;
    zlist_t *newjobs;
    struct job *job;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (!(newjobs = enqueue_jobs (queue, jobs))) {
        flux_log_error (h, "%s: error enqueuing batch", __FUNCTION__);
        goto error;
    }
    if (flux_respond (h, msg, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    flux_log (h, LOG_DEBUG, "%s: added %d jobs", __FUNCTION__,
                            (int)zlist_size (newjobs));
    /* Submitting user is being responded to with jobid's.
     * Now walk the list of new jobs and advance their state.
     */
    while ((job = zlist_pop (newjobs))) {
        job->state = FLUX_JOB_SCHED;
        if (alloc_do_request (alloc_ctx, job) < 0)
            flux_log_error (h, "%s: error notifying scheduler of new job %llu",
                            __FUNCTION__, (unsigned long long)job->id);
        job_decref (job);
    }
    zlist_destroy (&newjobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
