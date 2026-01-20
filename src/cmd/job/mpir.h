/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef FLUX_JOB_MPIR_H
#define FLUX_JOB_MPIR_H 1

void mpir_setup_interface (flux_t *h,
                           flux_jobid_t id,
                           bool debug_emulate,
                           bool stop_tasks_in_exec,
                           int leader_rank,
                           const char *shell_service);

void mpir_shutdown (flux_t *h);

#endif /* !FLUX_JOB_MPIR_H */
