/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timestamp.h"
#include "ccan/str/str.h"

/*
 *  GNU libc has timegm(3), but the manual states:
 *
 *   These functions [timelocal(), timegm()] are nonstandard GNU extensions
 *    that are also present on the BSDs.  Avoid their use.
 *
 *  This "portable" version was found on sourceware.org, and appears to work:
 *
 *   https://patchwork.sourceware.org/project/glibc/patch/20211011115406.11430-2-alx.manpages@gmail.com/
 *
 */
static time_t portable_timegm (struct tm *tm)
{
    time_t t;

    tm->tm_isdst = 0;
    if ((t = mktime (tm)) == (time_t) -1)
        return t;
    return t - timezone;
}

int timestamp_tostr (time_t t, char *buf, int size)
{
    struct tm tm;
    if (t < 0 || !gmtime_r (&t, &tm))
        return -1;
    if (strftime (buf, size, "%Y-%m-%dT%TZ", &tm) == 0)
        return -1;
    return 0;
}

int timestamp_fromstr (const char *s, time_t *tp)
{
    struct tm tm;
    time_t t;
    if (!strptime (s, "%Y-%m-%dT%TZ", &tm))
        return -1;
    if ((t = portable_timegm (&tm)) < 0)
        return -1;
    if (tp)
        *tp = t;
    return 0;
}

int timestamp_parse (const char *s,
                     struct tm *tm,
                     struct timeval *tv)
{
    char *extra;
    struct tm gm_tm;
    time_t t;

    if (s == NULL || (!tm && !tv)) {
        errno = EINVAL;
        return -1;
    }

    if (!(extra = strptime (s, "%Y-%m-%dT%T", &gm_tm))
        || (t = portable_timegm (&gm_tm)) < (time_t) -1) {
        errno = EINVAL;
        return -1;
    }

    if (tm && !(localtime_r (&t, tm)))
            return -1;

    if (tv)
        tv->tv_sec = t;

    if (tv && extra[0] == '.') {
        char *endptr;
        double d;

        errno = 0;
        d = strtod (extra, &endptr);

        /*  Note: in this implementation, there should be a "Z" after the
         *  timestamp to indicate UTC or "Zulu" time.
         */
        if (errno != 0 || *endptr != 'Z')
            return -1;

        /*  Note: cast to integer type truncates. To handle underflow from
         *  double arithmetic (e.g. result = 1234.999), add 0.5 and then
         *  allow the truncation to simulate floor(3).
         */
        tv->tv_usec = (d * 1000000) + 0.5;
    }
    return 0;
}

int timestamp_from_double (double ts, struct tm *tm, struct timeval *tv)
{
    if (ts < 0. || (!tm && !tv)) {
        errno = EINVAL;
        return -1;
    }
    if (tm) {
        time_t t = (time_t) ts;
        memset (tm, 0, sizeof (*tm));
        if (!localtime_r (&t, tm))
            return -1;
    }
    if (tv) {
        tv->tv_sec = ts;
        /*  Note: cast to integer type truncates. To handle underflow from
         *  double arithmetic (e.g. result = 1234.999), add 0.5 and then
         *  allow the truncation to simulate floor(3).
         */
        tv->tv_usec = ((ts - tv->tv_sec) * 1000000) + 0.5;
    }
    return 0;
}

int timestamp_tzoffset (struct tm *tm, char *buf, int len)
{
    if (!tm || !buf || len <= 0) {
        errno = EINVAL;
        return -1;
    }
    if (strftime (buf, len, "%z", tm) == 0)
        return -1;
    /* Special case: use "Z" for UTC for backwards compatibility
     */
    if (streq (buf, "+0000")) {
        buf[0] = 'Z';
        buf[1] = '\0';
        return 0;
    }
    /* O/w, insert `:` in offset if it is of the form [+-]NNNN for
     * enhanced readability.
     */
    if (strlen (buf) == 5
        && len >= 7
        && (buf[0] == '-' || buf[0] == '+')) {
        char minutes[3];

        /* Save last two characters of current tz string:
         * (Note: due to strlen check above, we know this only copies
         *  2 characters, so it is safe to use strcpy())
         */
        strcpy (minutes, buf+3);

        /* Insert colon after [+-]NN
         */
        buf[3] = ':';

        /* Copy minutes back to end of string.
         * (Note: we know minutes only contains 2 characters, so it is
         *  safe to use strcpy())
         */
        strcpy (buf+4, minutes);
    }
    return 0;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
