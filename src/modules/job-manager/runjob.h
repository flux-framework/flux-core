/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_JOB_MANAGER_RUNJOB_H
#define FLUX_JOB_MANAGER_RUNJOB_H

#include <flux/core.h>
#include "job-manager.h"

void runjob_handler (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg);

struct runjob *runjob_ctx_create (struct job_manager *ctx);
void runjob_ctx_destroy (struct runjob *runjob);


#endif /* !FLUX_JOB_MANAGER_RUNJOB_H */
