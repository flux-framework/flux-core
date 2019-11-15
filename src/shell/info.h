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
#include <flux/shell.h>

#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>

#include "jobspec.h"
#include "rcalc.h"

struct shell_info {
    flux_jobid_t jobid;
    int shell_rank;
    int shell_size;
    struct jobspec *jobspec;
    rcalc_t *rcalc;
    struct rcalc_rankinfo rankinfo;
};

/* Create shell_info.
 * Check for jobspec, R, and execution target rank on cmdline or fetch from
 *  job-info service
 */
struct shell_info *shell_info_create (flux_shell_t *shell);

void shell_info_destroy (struct shell_info *info);

#endif /* !_SHELL_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
