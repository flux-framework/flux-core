/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_CHECKPOINT_H
#define HAVE_JOB_EXEC_CHECKPOINT_H 1

#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "job-exec.h"

flux_future_t *checkpoint_get_rootrefs (flux_t *h);

char *checkpoint_find_rootref (flux_future_t *f,
                               flux_jobid_t id,
                               uint32_t owner);

void checkpoint_running (flux_t *h, zhashx_t *jobs);

#endif /* !HAVE_JOB_EXEC_CHECKPOINT_EXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
