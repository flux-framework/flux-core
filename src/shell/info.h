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
#include <flux/taskmap.h>
#include <flux/hostlist.h>

#include <jansson.h>
#include <stdbool.h>
#include <stdint.h>

#include "jobspec.h"
#include "rcalc.h"

struct shell_info {
    flux_jobid_t jobid;
    int shell_rank;
    int shell_size;
    int total_ntasks;
    json_t *R;
    struct jobspec *jobspec;
    rcalc_t *rcalc;
    struct rcalc_rankinfo rankinfo;
    struct taskmap *taskmap;
    struct idset *taskids;
    struct hostlist *hostlist;
    char *hwloc_xml;
    flux_future_t *R_watch_future;
};

/* Create shell_info.
 * Check for jobspec, R, and broker rank on cmdline or fetch from
 *  job-info service
 */
struct shell_info *shell_info_create (flux_shell_t *shell);

void shell_info_destroy (struct shell_info *info);

/*  Set or replace current shell taskmap and taskids idset
 *  Reference to `map` is stolen on success.
 */
int shell_info_set_taskmap (struct shell_info *info, struct taskmap *map);

#endif /* !_SHELL_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
