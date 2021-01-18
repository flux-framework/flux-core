/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* prioritize - job priority related functions
 *
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "job.h"
#include "event.h"
#include "alloc.h"
#include "job-manager.h"
#include "prioritize.h"

static int sched_prioritize (flux_t *h, json_t *priorities)
{
    flux_future_t *f;
    if (json_array_size (priorities) == 0) {
        json_decref (priorities);
        return 0;
    }
    if (!(f = flux_rpc_pack (h,
                             "sched.prioritize",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_NORESPONSE,
                             "{s:o}",
                             "jobs", priorities)))
        return -1;
    flux_future_destroy (f);
    return 0;
}

static int sched_prioritize_one (struct job_manager *ctx, struct job *job)
{
    json_t * priorities = json_pack ("[[II]]", job->id, job->priority);
    if (!priorities) {
        flux_log (ctx->h, LOG_ERR, "sched_prioritize: json_pack failed");
        return -1;
    }
    if (sched_prioritize (ctx->h, priorities) < 0) {
        flux_log_error (ctx->h, "rpc: sched.priority: id=%ju", job->id);
        json_decref (priorities);
        return -1;
    }
    return 0;
}

static int reprioritize_one (struct job_manager *ctx,
                             struct job *job,
                             int64_t priority,
                             bool oneshot)
{
    int flags = 0;

    /*  Urgency values that specify "hold" and "expedite" override
     *   requested priority:
     */
    if (job->urgency == FLUX_JOB_URGENCY_HOLD)
        priority = FLUX_JOB_PRIORITY_MIN;
    else if (job->urgency == FLUX_JOB_URGENCY_EXPEDITE)
        priority = FLUX_JOB_PRIORITY_MAX;

    /*  If priority did not change, then do not post a priority
     *   event, reorder queues, etc. (a change of priority to the
     *   current value is not an "event")
     */
    if (priority == job->priority)
        return 0;

    /*  Priority event is only posted to job eventlog in
     *   PRIORITY state, or transition of priority away from
     *   MIN (held) or MAX (expedited).
     */
    /*  XXX: Disable for now, tests assume all priority updates
     *       go to KVS eventlog
    if (!(job->flags & FLUX_JOB_DEBUG)
        && job->state != FLUX_JOB_STATE_PRIORITY
        && job->priority != FLUX_JOB_PRIORITY_MIN
        && job->priority != FLUX_JOB_PRIORITY_MAX)
        flags = EVENT_JOURNAL_ONLY;
     */

    /*  Post 'priority' event.
     *
     *  This call will result in job->priority being set, and,
     *   if the job is in the PRIORITY state, will transition to
     *   the SCHED state, invoke plugin callbacks, etc.
     */
    if (event_job_post_pack (ctx->event, job,
                             "priority",
                             flags,
                             "{s:I}",
                             "priority", priority) < 0)
        return -1;

    /*  Update alloc queues, cancel outstanding alloc requests for
     *   newly "held" jobs, and if in "oneshot" mode, notify scheduler
     *   of priority change
     */
    if (job->alloc_queued && oneshot) {
        alloc_queue_reorder (ctx->alloc, job);
        if (alloc_queue_recalc_pending (ctx->alloc) < 0)
            return -1;
    }
    else if (job->alloc_pending) {
        if (job->priority == FLUX_JOB_PRIORITY_MIN) {
            if (alloc_cancel_alloc_request (ctx->alloc, job) < 0)
                return -1;
        }
        else if (oneshot) {
            if (sched_prioritize_one (ctx, job) < 0)
                return -1;
        }
    }
    return 0;
}

int reprioritize_job (struct job_manager *ctx,
                      struct job *job,
                      int64_t priority)
{
    if (!job) {
        errno = EINVAL;
        return -1;
    }
    /*  For convenience, do not return an error if a job is not in
     *   a "prioritizable" state (PRIORITY || SCHED). Just do nothing.
     */
    if (job->state != FLUX_JOB_STATE_PRIORITY
        && job->state != FLUX_JOB_STATE_SCHED)
        return 0;
    return reprioritize_one (ctx, job, priority, true);
}

int reprioritize_id (struct job_manager *ctx,
                     flux_jobid_t id,
                     int64_t priority)
{
    struct job *job = zhashx_lookup (ctx->active_jobs, &id);
    if (!job) {
        errno = ENOENT;
        return -1;
    }
    return reprioritize_job (ctx, job, priority);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
