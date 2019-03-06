/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_UTIL_H
#define _FLUX_JOB_UTIL_H

#include <flux/core.h>
#include <stdbool.h>
#include <stdarg.h>

#include "job_types.h"

/* Write KVS path to 'key' relative to active job directory for job 'id'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int job_util_jobkey (char *buf, int bufsz, bool active,
                     flux_jobid_t id, const char *key);

#endif /* _FLUX_JOB_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

