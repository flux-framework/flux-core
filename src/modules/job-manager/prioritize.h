/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_PRIORITIZE_H
#define _FLUX_JOB_MANAGER_PRIORITIZE_H

#include <flux/core.h>
#include "job-manager.h"

/*  Request reprioritization of a single job
 */
int reprioritize_job (struct job_manager *ctx,
                      struct job *job,
                      int64_t priority);

int reprioritize_id (struct job_manager *ctx,
                     flux_jobid_t id,
                     int64_t priority);

#endif /* ! _FLUX_JOB_MANAGER_PRIORITIZE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
