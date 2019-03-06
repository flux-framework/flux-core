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

/* Look up 'key' relative to active/inactive job directory for job 'id'.
 */
flux_future_t *util_attr_lookup (flux_t *h, flux_jobid_t id, bool active,
                                 int flags, const char *key);

#endif /* _FLUX_JOB_MANAGER_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

