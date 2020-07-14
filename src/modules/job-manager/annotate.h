/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_ANNOTATE_H
#define _FLUX_JOB_MANAGER_ANNOTATE_H

#include <stdint.h>

#include "job-manager.h"

void annotations_clear (struct job *job, bool *cleared);
void annotations_update (flux_t *h, struct job *job, json_t *annotations);

struct annotate *annotate_ctx_create (struct job_manager *ctx);
void annotate_ctx_destroy (struct annotate *annotate);

#endif /* ! _FLUX_JOB_MANAGER_ANNOTATE_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
