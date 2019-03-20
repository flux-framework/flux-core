/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_SUBMIT_H
#define _FLUX_JOB_MANAGER_SUBMIT_H

#include <stdbool.h>
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "queue.h"
#include "alloc.h"

struct submit_ctx;

void submit_ctx_destroy (struct submit_ctx *ctx);
struct submit_ctx *submit_ctx_create (flux_t *h,
                                      struct queue *queue,
                                      struct event_ctx *event_ctx);

void submit_enable (struct submit_ctx *ctx);
void submit_disable (struct submit_ctx *ctx);

/* exposed for unit testing only */
int submit_enqueue_one_job (struct queue *queue, zlist_t *newjobs, json_t *o);
void submit_enqueue_jobs_cleanup (struct queue *queue, zlist_t *newjobs);
zlist_t *submit_enqueue_jobs (struct queue *queue, json_t *jobs);
int submit_post_event (struct event_ctx *event_ctx, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_SUBMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
