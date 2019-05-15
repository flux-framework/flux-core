/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_START_H
#define _FLUX_JOB_MANAGER_START_H

#include <flux/core.h>

#include "queue.h"
#include "job.h"
#include "event.h"

struct event_ctx;
struct start_ctx;

void start_ctx_destroy (struct start_ctx *ctx);
struct start_ctx *start_ctx_create (flux_t *h,
                                    struct queue *queue,
                                    struct event_ctx *event_ctx);

int start_send_request (struct start_ctx *ctx, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_START_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
