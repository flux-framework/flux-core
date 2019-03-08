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

#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "queue.h"
#include "alloc.h"

void submit_handle_request (flux_t *h,
                            struct queue *queue,
                            struct event_ctx *event_ctx,
                            const flux_msg_t *msg);

/* exposed for unit testing only */
int submit_enqueue_one_job (struct queue *queue, zlist_t *newjobs, json_t *o);
void submit_enqueue_jobs_cleanup (struct queue *queue, zlist_t *newjobs);
zlist_t *submit_enqueue_jobs (struct queue *queue, json_t *jobs);
int submit_post_event (struct event_ctx *event_ctx, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_SUBMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
