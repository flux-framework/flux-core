/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_VALIDATE_H
#define _FLUX_JOB_MANAGER_VALIDATE_H

#include "job-manager.h"

struct validate *validate_ctx_create (struct job_manager *ctx);
void validate_ctx_destroy (struct validate *raise);

#endif /* ! _FLUX_JOB_MANAGER_VALIDATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
