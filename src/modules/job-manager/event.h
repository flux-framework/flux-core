/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_EVENT_H
#define _FLUX_JOB_MANAGER_EVENT_H

#include <stdarg.h>
#include <flux/core.h>

#include "job.h"
#include "alloc.h"
#include "start.h"

struct event_ctx;
struct alloc_ctx;

/* Take any action for 'job' currently needed based on its internal state.
 * Returns 0 on success, -1 on failure with errno set.
 * This function is idempotent.
 */
int event_job_action (struct event_ctx *ctx, struct job *job);

/* Call to update 'job' internal state based on 'event'.
 * Returns 0 on success, -1 on failure with errno set.
 */
int event_job_update (struct job *job, const char *event);

/* Post event 'name' and optionally 'context' to 'job'.
 * Internally, calls event_job_update(), then event_job_action(), then commits
 * the event to job KVS eventlog.  The KVS commit completes asynchronously.
 * The future passed in as an argument should not be destroyed.
 * Returns 0 on success, -1 on failure with errno set.
 */
int event_job_post (struct event_ctx *ctx, struct job *job,
                    const char *name, const char *context);

/* Same as above except event context is constructed from (fmt, ...)
 * using style of jansson's json_pack().
 */
int event_job_post_pack (struct event_ctx *ctx, struct job *job,
                         const char *name, const char *fmt, ...);

void event_ctx_set_alloc_ctx (struct event_ctx *ctx,
                              struct alloc_ctx *alloc_ctx);
void event_ctx_set_start_ctx (struct event_ctx *ctx,
                              struct start_ctx *start_ctx);

void event_ctx_destroy (struct event_ctx *ctx);
struct event_ctx *event_ctx_create (flux_t *h, struct queue *queue);

#endif /* _FLUX_JOB_MANAGER_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

