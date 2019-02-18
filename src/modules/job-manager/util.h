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
#include <stdarg.h>

/* Parse key=val integer from event context.  val may be NULL.
 * Return 0 on success, -1 on failure with errno set.
 */
int util_int_from_context (const char *context, const char *key, int *val);
/* Parse key=val string from event context.  val may be NULL.
 * Return 0 on success, -1 on failure with errno set.
 */
int util_str_from_context (const char *context, const char *key,
                           char *val, int valsize);
/* Parse trailing non key=val context.  Context must not contain \n.
 * NULL if there is none.
 */
const char *util_note_from_context (const char *context);

/* Write KVS path to 'key' relative to active job directory for job 'id'.
 * If key=NULL, write the job directory.
 * Returns string length on success, or -1 on failure.
 */
int util_jobkey (char *buf, int bufsz, bool active,
                 flux_jobid_t id, const char *key);

/* Set 'key' within active job directory for job 'id'.
 */
int util_attr_pack (flux_kvs_txn_t *txn,
                    flux_jobid_t id,
                    const char *key,
                    const char *fmt, ...);

/* Log an event to eventlog in active job directory for job 'id'.
 * The event consists of current wallclock, 'name', and optional context
 * formatted from (fmt, ...).  Set fmt="" to skip logging a context.
 */
int util_eventlog_append (flux_kvs_txn_t *txn,
                          flux_jobid_t id,
                          const char *name,
                          const char *fmt, ...);

/* Look up 'key' relative to active/inactive job directory for job 'id'.
 */
flux_future_t *util_attr_lookup (flux_t *h, flux_jobid_t id, bool active,
                                 int flags, const char *key);

#endif /* _FLUX_JOB_MANAGER_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

