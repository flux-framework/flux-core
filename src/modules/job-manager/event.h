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

struct event_ctx;
struct alloc_ctx;

typedef void (*event_completion_f)(flux_future_t *f, void *arg);

/* Take any action for 'job' currently needed based on its internal state.
 */
int event_job_action (struct event_ctx *ctx, struct job *job);

/* Call to update 'job' internal state based on 'event'.
 */
int event_job_update (struct job *job, const char *event);

/* Log event (name, context) to jobs.active.<jobid>.eventlog.
 * Once event is committed, cb(f, arg) is invoked if cb != NULL.
 * The callback may call flux_future_get() to determine if the commit
 * succeeded.
 */
int event_log (struct event_ctx *ctx, struct job *job,
               event_completion_f cb, void *arg,
               const char *name, const char *context);

/* Same as above except event context is constructed from (fmt, ...).
 */
int event_log_fmt (struct event_ctx *ctx, struct job *job,
                   event_completion_f cb, void *arg,
                   const char *name, const char *fmt, ...);

void event_ctx_set_alloc_ctx (struct event_ctx *ctx,
                              struct alloc_ctx *alloc_ctx);

void event_ctx_destroy (struct event_ctx *ctx);
struct event_ctx *event_ctx_create (flux_t *h);

#endif /* _FLUX_JOB_MANAGER_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

