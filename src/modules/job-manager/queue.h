/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_QUEUE_H
#define _FLUX_JOB_MANAGER_QUEUE_H

#include <stdbool.h>

#include "job-manager.h"

struct queue *queue_create (struct job_manager *ctx);
void queue_destroy (struct queue *queue);

int queue_submit_check (struct queue *queue,
                        json_t *jobspec,
                        flux_error_t *error);


#endif /* ! _FLUX_JOB_MANAGER_QUEUE_H */

// vi:ts=4 sw=4 expandtab
