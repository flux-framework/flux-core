/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_RAISE_H
#define _FLUX_JOB_MANAGER_RAISE_H

#include <stdint.h>

#include "job-manager.h"

struct raise *raise_ctx_create (struct job_manager *ctx);
void raise_ctx_destroy (struct raise *raise);

/* exposed for unit testing only */
int raise_check_type (const char *type);
int raise_check_severity (int severity);

/* Raise a job exception: post to job eventlog and publish job-exception
 * message.
 *
 * N.B. job object may be destroyed in event_job_post_pack().
 * Do not reference the object after calling this function.
 * Do not call this function and continue to iterate on the job hash
 * with zhash_next().
 */
int raise_job_exception (struct job_manager *ctx,
                         struct job *job,
                         const char *type,
                         int severity,
                         uint32_t userid,   // skip if FLUX_USERID_NONE
                         const char *note); // skip if NULL


#endif /* ! _FLUX_JOB_MANAGER_RAISE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
