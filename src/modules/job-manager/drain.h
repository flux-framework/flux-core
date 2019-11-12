/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_DRAIN_H
#define _FLUX_JOB_MANAGER_DRAIN_H

#include <stdbool.h>
#include <flux/core.h>

#include "job-manager.h"

struct drain *drain_ctx_create (struct job_manager *ctx);
void drain_ctx_destroy (struct drain *drain);

#endif /* ! _FLUX_JOB_MANAGER_DRAIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
