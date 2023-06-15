/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_CONFIG_H
#define HAVE_JOB_EXEC_CONFIG_H 1

#include <flux/core.h>

#include "job-exec.h"

const char *config_get_job_shell (struct jobinfo *job);

const char *config_get_cwd (struct jobinfo *job);

const char *config_get_imp_path (void);

int config_init (flux_t *h, int argc, char **argv);

#endif /* !HAVE_JOB_EXEC_CONFIG_EXEC_H */

/* vi: ts=4 sw=4 expandtab
 */
