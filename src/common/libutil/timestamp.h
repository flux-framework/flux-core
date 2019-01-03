/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_TIMESTAMP_H
#define _UTIL_TIMESTAMP_H

#include <time.h>

/* Convert time_t (GMT) to ISO 8601 timestamp string,
 * e.g. "2003-08-24T05:14:50Z"
 */
int timestamp_tostr (time_t t, char *buf, int size);

/* Convert from ISO 8601 string to time_t.
 */
int timestamp_fromstr (const char *s, time_t *tp);


#endif /* !_UTIL_TIMESTAMP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
