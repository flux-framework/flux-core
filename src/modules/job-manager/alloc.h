/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_ALLOC_H
#define _FLUX_JOB_MANAGER_ALLOC_H

#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "job-manager.h"

void alloc_ctx_destroy (struct alloc *alloc);
struct alloc *alloc_ctx_create (struct job_manager *ctx);

/* Call from SCHED state to put job in queue to request resources.
 * This function is a no-op if job->alloc_queued or job->alloc_pending is set.
 */
int alloc_enqueue_alloc_request (struct alloc *alloc, struct job *job);

/* Dequeue job from sched inqueue, e.g. on exception.
 * This function is a no-op if job->alloc_queued is not set.
 */
void alloc_dequeue_alloc_request (struct alloc *alloc, struct job *job);

/* Send a request to cancel pending alloc request.
 * This function is a no-op if job->alloc_pending is not set.
 * If finalize is true, update the job as though the cancelation
 * request has already been handled, so the job can progress through
 * CLEANUP without waiting for the scheduler response.
 */
int alloc_cancel_alloc_request (struct alloc *alloc,
                                struct job *job,
                                bool finalize);

/* Accessor for the count of queued alloc requests.
 */
int alloc_queue_count (struct alloc *alloc);

/* Accessor for the count of pending alloc requests.
 */
int alloc_pending_count (struct alloc *alloc);

/* Release resources back to the scheduler.
 */
int alloc_send_free_request (struct alloc *alloc,
                             json_t *R,
                             flux_jobid_t id,
                             bool final);

/* List pending jobs
 */
struct job *alloc_queue_first (struct alloc *alloc);
struct job *alloc_queue_next (struct alloc *alloc);

/* Reorder job in scheduler queue, e.g. after urgency change.
 */
void alloc_queue_reorder (struct alloc *alloc, struct job *job);

/* Reorder job in pending jobs queue, e.g. after urgency change.
 */
void alloc_pending_reorder (struct alloc *alloc, struct job *job);

/* Re-sort alloc queue and pending jobs.
 * Recalculate pending jobs if necessary
 */
int alloc_queue_reprioritize (struct alloc *alloc);

/* Recalculate pending job, e.g. after urgency change */
int alloc_queue_recalc_pending (struct alloc *alloc);

void alloc_disconnect_rpc (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg);

bool alloc_sched_ready (struct alloc *alloc);

#endif /* ! _FLUX_JOB_MANAGER_ALLOC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
