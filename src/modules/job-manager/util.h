/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_UTIL_H
#define _FLUX_JOB_MANAGER_UTIL_H

#include <flux/core.h>
#include <stdbool.h>
#include "job.h"

/* Write KVS path to 'key' relative to active job directory for 'job'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int util_jobkey (char *buf, int bufsz, bool active,
                 struct job *job, const char *key);

/* Set 'key' within active job directory for 'job'.
 */
int util_attr_pack (flux_kvs_txn_t *txn,
                    struct job *job,
                    const char *key,
                    const char *fmt, ...);

/* Log an event to eventlog in active job directory for 'job'.
 * The event consists of current wallclock, 'name', and optional context
 * formatted from (fmt, ...).  Set fmt="" to skip logging a context.
 */
int util_eventlog_append (flux_kvs_txn_t *txn,
                          struct job *job,
                          const char *name,
                          const char *fmt, ...);

#endif /* _FLUX_JOB_MANAGER_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

