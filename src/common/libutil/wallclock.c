/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "wallclock.h"

/* Generate ISO 8601 timestamp that additionally conforms to RFC 5424 (syslog).
 *
 * Examples from RFC 5424:
 *   1985-04-12T23:20:50.52Z
 *   1985-04-12T19:20:50.52-04:00
 *   2003-10-11T22:14:15.003Z
 *   2003-08-24T05:14:15.000003-07:00
 */

int wallclock_get_zulu (char *buf, size_t len)
{
    struct timespec ts;
    struct tm tm;
    time_t t;

    if (len < WALLCLOCK_MAXLEN) {
        errno = EINVAL;
        return -1;
    }
    if (clock_gettime (CLOCK_REALTIME, &ts) < 0)
        return -1;
    t = ts.tv_sec;
    if (!gmtime_r (&t, &tm)) {
        errno = EINVAL;
        return -1;
    }
    if (strftime (buf, len, "%FT%T", &tm) == 0) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf (buf + 19, len - 19, ".%.6luZ", ts.tv_nsec / 1000)
        >= len - 20) {
        errno = EINVAL;
        return -1;
    }
    return strlen (buf);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
