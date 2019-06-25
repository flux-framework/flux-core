/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_INFO_H
#define _SHELL_INFO_H

#include <flux/core.h>
#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>

#include "jobspec.h"
#include "rcalc.h"

struct shell_info {
    flux_jobid_t jobid;
    int shell_rank;
    int shell_size;
    bool verbose;
    struct jobspec *jobspec;
    rcalc_t *rcalc;
    struct rcalc_rankinfo rankinfo;
};

/* Create shell_info.
 * If jobspec or R are NULL, or broker_rank == -1, then
 * missing info is fetched from the Flux instance.
 * Otherwise h may be NULL.
 */
struct shell_info *shell_info_create (flux_t *h,
                                      flux_jobid_t jobid,
                                      int broker_rank,
                                      const char *jobspec,
                                      const char *R,
                                      bool verbose);

void shell_info_destroy (struct shell_info *info);

#endif /* !_SHELL_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
