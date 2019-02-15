/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_RESTART_H
#define _FLUX_JOB_MANAGER_RESTART_H

#include <flux/core.h>
#include "job.h"

/* restart_map callback should return -1 on error to stop map with error,
 * or 0 on success.  'job' is only valid for the duration of the callback.
 */
typedef int (*restart_map_f)(struct job *job, void *arg);

/* call 'cb' once for each job found in active job directory.
 * Returns number of jobs mapped, or -1 on error.
 */
int restart_map (flux_t *h, restart_map_f cb, void *arg);

/* exposed for unit testing only */
int restart_decode_exception_severity (const char *s);
int restart_count_char (const char *s, char c);
int restart_replay_eventlog (const char *s, double *t_submit,
                             int *flagsp, int *statep);

#endif /* _FLUX_JOB_MANAGER_RESTART_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

