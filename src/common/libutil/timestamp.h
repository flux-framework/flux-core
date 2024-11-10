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

#include <sys/time.h>
#include <time.h>

/* Convert time_t (GMT) to ISO 8601 timestamp string,
 * e.g. "2003-08-24T05:14:50Z"
 */
int timestamp_tostr (time_t t, char *buf, int size);

/* Convert from ISO 8601 string to time_t.
 */
int timestamp_fromstr (const char *s, time_t *tp);

/* Convert from ISO 8601 timestamp string, including optional
 * microsecond precision, to struct tm, timeval pair.
 *
 * e.g. "2022-10-15T14:43:18.159009Z"
 *
 * At least one of 'tm' or 'tv' must be provided.
 */
int timestamp_parse (const char *s,
                     struct tm *tm,
                     struct timeval *tv);


/* Convert a double precision timestamp to struct tm and timeval.
 * At least one of 'tm' or 'tv' must be provided.
 */
int timestamp_from_double (double ts, struct tm *tm, struct timeval *tv);

/* Get the current timezone offset for `tm` in the form [+-]HH:MM
 * and place it into the supplied buffer. As a special case, +00:00
 * is converted to "Z" (Zulu time) for backwards compatibility when
 * the current timezone is UTC.
 *
 * Returns -1 on failure, 0 for success.
 */
int timestamp_tzoffset (struct tm *tm, char *buf, int size);

#endif /* !_UTIL_TIMESTAMP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
