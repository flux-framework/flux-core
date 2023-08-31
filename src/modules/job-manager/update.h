/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_UPDATE_H
#define _FLUX_JOB_MANAGER_UPDATE_H

#include "job-manager.h"

struct update *update_ctx_create (struct job_manager *ctx);
void update_ctx_destroy (struct update *update);

#endif /* ! _FLUX_JOB_MANAGER_UPDATE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
