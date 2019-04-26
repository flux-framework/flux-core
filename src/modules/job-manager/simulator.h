/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_SIMULATOR_H
#define _FLUX_JOB_MANAGER_SIMULATOR_H

#include <flux/core.h>

#include "job-manager.h"

struct simulator;

void sim_ctx_destroy (struct simulator *ctx);
struct simulator *sim_ctx_create (struct job_manager *ctx);

/* Call when sending a new RPC/work to the scheduler.
 * Triggers a new 'quiescent' RPC to the scheduler and destroys any
 * outstanding 'quiescent' requests.
 */
void sim_sending_sched_request (struct simulator *ctx);

#endif /* ! _FLUX_JOB_MANAGER_SIMULATOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
