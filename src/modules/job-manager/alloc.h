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

#include <flux/core.h>

#include "queue.h"
#include "job.h"
#include "event.h"

struct alloc_ctx;
struct event_ctx;

void alloc_ctx_destroy (struct alloc_ctx *ctx);
struct alloc_ctx *alloc_ctx_create (flux_t *h, struct queue *queue,
                                    struct event_ctx *event_ctx);

/* Call from SCHED state to put job in queue to request resources.
 * This function is a no-op if job->alloc_queued or job->alloc_pending is set.
 */
int alloc_enqueue_alloc_request (struct alloc_ctx *ctx, struct job *job);

/* Dequeue job from sched inqueue, e.g. on exception.
 * This function is a no-op if job->alloc_queued is not set.
 */
void alloc_dequeue_alloc_request (struct alloc_ctx *ctx, struct job *job);

/* Call from CLEANUP state to release resources.
 * This function is a no-op if job->free_pending is set.
 */
int alloc_send_free_request (struct alloc_ctx *ctx, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_ALLOC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
