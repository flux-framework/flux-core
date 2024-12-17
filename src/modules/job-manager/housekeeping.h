/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_HOUSEKEEPING_H
#define _FLUX_JOB_MANAGER_HOUSEKEEPING_H

#include <flux/core.h>
#include "src/common/librlist/rlist.h"
#include "job-manager.h"

struct housekeeping *housekeeping_ctx_create (struct job_manager *ctx);
void housekeeping_ctx_destroy (struct housekeeping *hk);

/* Call this to transfer a job's R to the housekeeping subsystem.  The job
 * may treat R as freed, but R will remain allocated from the scheduler's
 * perspective until the housekeeping script is run on each execution target.
 */
int housekeeping_start (struct housekeeping *hk,
                        json_t *R,
                        flux_jobid_t id,
                        uint32_t userid);

/* Call this to add responses to the scheduler's hello request at startup.
 * It should inform the scheduler about resources that are still allocated,
 * but no longer directly held by jobs.
 */
int housekeeping_hello_respond (struct housekeeping *hk,
                                const flux_msg_t *msg,
                                bool partial_ok);

json_t *housekeeping_get_stats (struct housekeeping *hk);

int housekeeping_stat_append (struct housekeeping *hk,
                              struct rlist *rl,
                              flux_error_t *error);


#endif /* ! _FLUX_JOB_MANAGER_HOUSEKEEPING_H */

// vi:ts=4 sw=4 expandtab
