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

struct shell_info {
    flux_jobid_t jobid;
    uint32_t broker_rank;
    json_t *jobspec;
    struct resource_set *rset;
    json_t *rlocal;
    bool verbose;
};

/* Create shell_info.
 * If jobspec and/or R are non-NULL, use them instead of the ones
 * placed in the KVS for the job (intended for testing).
 */
struct shell_info *shell_info_create (flux_t *h,
                                      flux_jobid_t jobid,
                                      uint32_t broker_rank,
                                      const char *jobspec,
                                      const char *R,
                                      bool verbose);

void shell_info_destroy (struct shell_info *info);

#endif /* !_SHELL_INFO_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
