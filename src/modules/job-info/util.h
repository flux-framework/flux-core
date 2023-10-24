/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_UTIL_H
#define _FLUX_JOB_INFO_UTIL_H

#include <jansson.h>
#include <flux/core.h>

/* we want to copy credentials, etc. from the original
 * message when we send RPCs to other job-info targets.
 */
flux_msg_t *cred_msg_pack (const char *topic,
                           struct flux_msg_cred cred,
                           const char *fmt,
                           ...);

/* helper to parse next eventlog entry when whole eventlog is read */
bool eventlog_parse_next (const char **pp, const char **tok,
                          size_t *toklen);

/* parse chunk from eventlog_parse_next, 'entry' is required and
 * should be json_decref'ed after use */
int eventlog_parse_entry_chunk (flux_t *h,
                                const char *tok,
                                size_t toklen,
                                json_t **entry,
                                const char **name,
                                json_t **context);

#endif /* ! _FLUX_JOB_INFO_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
