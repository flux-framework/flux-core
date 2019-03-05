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

void alloc_ctx_destroy (struct alloc_ctx *ctx);
struct alloc_ctx *alloc_ctx_create (flux_t *h, struct queue *queue,
                                    struct event_ctx *event_ctx);

/* Call this from other parts of the job manager when the alloc
 * machinery might need to take action on 'job'.  The action taken
 * depends on job state and internal flags.
 */
int alloc_do_request (struct alloc_ctx *scd, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_ALLOC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
