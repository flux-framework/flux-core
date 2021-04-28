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
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "job-manager.h"

struct submit *submit_ctx_create (struct job_manager *ctx);
void submit_ctx_destroy (struct submit *submit);

/* exposed for unit testing only */
void submit_add_jobs_cleanup (zhashx_t *active_jobs, zlistx_t *newjobs);
zlistx_t *submit_jobs_to_list (json_t *jobs);
int submit_hash_jobs (zhashx_t *active_jobs, zlistx_t *newjobs);

#endif /* ! _FLUX_JOB_MANAGER_SUBMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
