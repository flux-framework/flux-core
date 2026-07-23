/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_CMD_H
#define HAVE_JOB_EXEC_CMD_H 1

#include <flux/core.h>

#include "job-exec.h"

/*  Build the base job-shell command for `job` to be launched via `service`
 *  ("rexec" or "sdexec").  The returned command covers the common case where
 *  a single command is pushed for all ranks: it carries the guest KVS
 *  namespace, any configured sdexec properties, the IMP wrapper for multiuser
 *  jobs, and the shell argv (shell path + jobid).  Per-rank sdexec options
 *  (resource constraints) are applied separately by the caller.
 *
 *  Returns a new flux_cmd_t on success (caller destroys it), or NULL on
 *  failure with an error logged to job->h.
 */
flux_cmd_t *job_shell_cmd_create (struct jobinfo *job, const char *service);

#endif /* !HAVE_JOB_EXEC_CMD_H */

/* vi: ts=4 sw=4 expandtab
 */
