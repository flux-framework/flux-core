/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_KILL_H
#define _FLUX_JOB_MANAGER_KILL_H

#include <stdint.h>
#include "job-manager.h"

struct kill *kill_ctx_create (struct job_manager *ctx);
void kill_ctx_destroy (struct kill *kill);


/* exposed for unit testing only */
int kill_check_signal (int signum);

#endif /* ! _FLUX_JOB_MANAGER_RAISE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
