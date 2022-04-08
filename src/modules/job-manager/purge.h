/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_PURGE_H
#define _FLUX_JOB_MANAGER_PURGE_H

#include <stdbool.h>
#include <flux/core.h>

#include "job-manager.h"

struct purge *purge_create (struct job_manager *ctx);
void purge_destroy (struct purge *purge);

int purge_enqueue_job (struct purge *purge, struct job *job);

#endif /* ! _FLUX_JOB_MANAGER_PURGE_H */

// vi:ts=4 sw=4 expandtab
