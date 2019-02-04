/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_ACTIVE_H
#define _FLUX_JOB_MANAGER_ACTIVE_H

/* Operations on active jobs in KVS
 */

#include <flux/core.h>
#include "job.h"

/* Write KVS path to 'key' relative to active job directory for 'job'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int active_key (char *buf, int bufsz, struct job *job, const char *key);

/* Set 'key' within active job directory for 'job'.
 */
int active_pack (flux_kvs_txn_t *txn,
                 struct job *job,
                 const char *key,
                 const char *fmt, ...);

/* Log an event to eventlog 'key', relative to active job directory for 'job'.
 * The event consists of current wallclock, 'name', and optional context
 * formatted from (fmt, ...).  Set fmt="" to skip logging a context.
 */
int active_eventlog_append (flux_kvs_txn_t *txn,
                            struct job *job,
                            const char *key,
                            const char *name,
                            const char *fmt, ...);

/* Unlink the active job directory for 'job'.
 */
int active_unlink (flux_kvs_txn_t *txn, struct job *job);

#endif /* _FLUX_JOB_MANAGER_ACTIVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

