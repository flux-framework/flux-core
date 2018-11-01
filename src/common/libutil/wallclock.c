/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
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
    if (snprintf (buf+19, len-19, ".%.6luZ", ts.tv_nsec/1000) >= len - 20) {
        errno = EINVAL;
        return -1;
    }
    return strlen (buf);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
