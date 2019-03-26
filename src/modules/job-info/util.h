/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_UTIL_H
#define _FLUX_JOB_INFO_UTIL_H

/* 'pp' is an in/out parameter pointing to input buffer.
 * Set 'tok' to next \n-terminated token, and 'toklen' to its length.
 * Advance 'pp' past token.  Returns false when input is exhausted.
 */
bool eventlog_parse_next (const char **pp, const char **tok,
                          size_t *toklen);

#endif /* ! _FLUX_JOB_INFO_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
