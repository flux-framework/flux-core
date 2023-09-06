/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_JOB_STATS_H
#define _FLUX_JOB_LIST_JOB_STATS_H

#include <flux/core.h> /* FLUX_JOB_NR_STATES */
#include <jansson.h>

#include "job_data.h"

struct job_stats {
    unsigned int state_count[FLUX_JOB_NR_STATES];
    unsigned int successful;
    unsigned int failed;
    unsigned int timeout;
    unsigned int canceled;
    unsigned int inactive_purged;
};

struct job_stats_ctx *job_stats_ctx_create (flux_t *h);

void job_stats_ctx_destroy (struct job_stats_ctx *statsctx);

void job_stats_update (struct job_stats_ctx *statsctx,
                       struct job *job,
                       flux_job_state_t newstate);

void job_stats_add_queue (struct job_stats_ctx *statsctx,
                          struct job *job);

void job_stats_remove_queue (struct job_stats_ctx *statsctx,
                             struct job *job);

void job_stats_purge (struct job_stats_ctx *statsctx, struct job *job);

#endif /* ! _FLUX_JOB_LIST_JOB_STATS_H */

// vi: ts=4 sw=4 expandtab
