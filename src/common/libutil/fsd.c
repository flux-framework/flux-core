/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "fsd.h"
#include "ccan/str/str.h"

static int is_invalid_duration (double d)
{
    switch (fpclassify (d)) {
        case FP_NORMAL:     // [[fallthrough]]
        case FP_SUBNORMAL:  // [[fallthrough]]
        case FP_ZERO:       // [[fallthrough]]
        case FP_INFINITE:   // [[fallthrough]]
            break;          // OK
        case FP_NAN:        // [[fallthrough]]
        default:            // something else, bad
            errno = EINVAL;
            return -1;
    }

    if (d < 0.) {
        errno = EINVAL;
        return -1;
    }

    return 0;
}

int fsd_parse_duration (const char *s, double *dp)
{
    double d;
    char *p;
    if (s == NULL || dp == NULL) {
        errno = EINVAL;
        return -1;
    }
    d = strtod (s, &p);

    if (is_invalid_duration (d))
        return -1;

    if (*p != '\0') {
        double multiplier = 0.;

        /*  units not allowed for inf/infinity
         */
        if (isinf (d)) {
            errno = EINVAL;
            return -1;
        }

        if (streq (p, "ms"))
            multiplier = .001;
        else if (streq (p, "s"))
            multiplier = 1;
        else if (streq (p, "m"))
            multiplier = 60;
        else if (streq (p, "h"))
            multiplier = 60 * 60;
        else if (streq (p, "d"))
            multiplier = 60 * 60 * 24;
        else  {
            errno = EINVAL;
            return -1;
        }
        d *= multiplier;
    }
    *dp = d;
    return 0;
}

int fsd_format_duration_ex (char *buf,
                            size_t len,
                            double duration,
                            int precision)
{
    if (buf == NULL || len <= 0) {
        errno = EINVAL;
        return -1;
    }

    /*  First check for infinity special case
     */
    if (isinf (duration))
        return snprintf (buf, len, "%s", "infinity");

    if (is_invalid_duration (duration)) {
        errno = EINVAL;
        return -1;
    }

    /*  We'd rather present a result in seconds if possible, since that
     *  is the base unit of FSD. However, if the duration is very small,
     *  present in milliseconds since the result will be easier for a
     *  human to read. E.g. 62.1ms vs 0.0621s, or more importantly
     *  0.0123ms vs 1.23e-05s.
     */
    if (duration < 0.1 && duration != 0.)
        return snprintf (buf, len, "%.*gms", precision, duration * 1000.);
    else if (duration < 60.)
        return snprintf (buf, len, "%.*gs", precision, duration);
    else if (duration < 60. * 60.)
        return snprintf (buf, len, "%.*gm", precision, duration / 60.);
    else if (duration < 60. * 60. * 24.)
        return snprintf (buf, len, "%.*gh", precision, duration / (60. * 60.));
    else {
        return snprintf (buf, len, "%.*gd", precision,
                         duration / (60. * 60. * 24.));
    }
}

int fsd_format_duration (char *buf, size_t len, double duration)
{
    return fsd_format_duration_ex (buf, len, duration, 6);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
