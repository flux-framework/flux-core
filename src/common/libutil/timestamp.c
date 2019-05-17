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
#    include <config.h>
#endif /* HAVE_CONFIG_H */

#include <time.h>

#include "timestamp.h"

int timestamp_tostr (time_t t, char *buf, int size)
{
    struct tm tm;
    if (t < 0 || !gmtime_r (&t, &tm))
        return -1;
    if (strftime (buf, size, "%FT%TZ", &tm) == 0)
        return -1;
    return 0;
}

int timestamp_fromstr (const char *s, time_t *tp)
{
    struct tm tm;
    time_t t;
    if (!strptime (s, "%FT%TZ", &tm))
        return -1;
    if ((t = timegm (&tm)) < 0)
        return -1;
    if (tp)
        *tp = t;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
