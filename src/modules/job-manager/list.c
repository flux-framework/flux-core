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
 *   List active jobs.  This is useful for testing the job-manager.
 *
 * Input:
 * - max number of jobs to return from head of queue
 *
 * Output:
 * - array of job objects
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "list.h"
#include "alloc.h"
#include "wait.h"
#include "job-manager.h"


int list_append_job (json_t *jobs, struct job *job)
{
    json_t *o;

    /* We pack priority with I instead of i to avoid issue of
     * signed vs unsigned int */
    if (!(o = json_pack ("{s:I s:I s:i s:I s:f s:i}",
                         "id", job->id,
                         "userid", (json_int_t) job->userid,
                         "urgency", job->urgency,
                         "priority", job->priority,
                         "t_submit", job->t_submit,
                         "state", job->state))) {
        errno = ENOMEM;
        return -1;
    }
    if (job->annotations) {
        if (json_object_set (o, "annotations", job->annotations) < 0) {
            json_decref (o);
            errno = ENOMEM;
            return -1;
        }
    }
    if (job->dependencies) {
        json_t *deps = grudgeset_tojson (job->dependencies);
        if (json_object_set (o, "dependencies", deps) < 0) {
            json_decref (o);
            errno = ENOMEM;
            return -1;
        }
    }
    if (json_array_append_new (jobs, o) < 0) {
        json_decref (o);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void list_handle_request (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_manager *ctx = arg;
    int max_entries;
    json_t *jobs = NULL;
    struct job *job;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:i}",
                             "max_entries", &max_entries) < 0)
        goto error;
    if (max_entries < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(jobs = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    /* First list jobs in SCHED (S) state
     * (urgency, then job id order).
     */
    job = alloc_queue_first (ctx->alloc);
    while (job && (max_entries == 0 || json_array_size (jobs) < max_entries)) {
        if (list_append_job (jobs, job) < 0)
            goto error;
        job = alloc_queue_next (ctx->alloc);
    }
    /* Then list remaining active jobs - DEPEND (D), RUN (R), CLEANUP (C)
     * (random order).
     */
    job = zhashx_first (ctx->active_jobs);
    while (job && (max_entries == 0 || json_array_size (jobs) < max_entries)) {
        if (!job->alloc_queued) {
            if (list_append_job (jobs, job) < 0)
                goto error;
        }
        job = zhashx_next (ctx->active_jobs);
    }
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
