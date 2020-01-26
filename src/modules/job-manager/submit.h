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

#include "job.h"
#include "job-manager.h"

struct submit *submit_ctx_create (struct job_manager *ctx);
void submit_ctx_destroy (struct submit *submit);

/* exposed for unit testing only */
int submit_add_one_job (zhashx_t *active_jobs, zlist_t *newjobs, json_t *o);
void submit_add_jobs_cleanup (zhashx_t *active_jobs, zlist_t *newjobs);
zlist_t *submit_add_jobs (zhashx_t *active_jobs, json_t *jobs);
int submit_post_event (struct event *event, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_SUBMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
