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

static int is_invalid_duration (double d)
{
    switch (fpclassify (d)) {
        case FP_NORMAL:     // [[fallthrough]]
        case FP_SUBNORMAL:  // [[fallthrough]]
        case FP_ZERO:       // [[fallthrough]]
            break;          // OK
        case FP_INFINITE:   // [[fallthrough]]
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

    if (is_invalid_duration (d)) {
        return -1;
    }

    if (*p && *(p + 1)) {
        errno = EINVAL;
        return -1;
    }

    if (*p != '\0') {
        unsigned int multiplier = 0;
        switch (*p) {
            case 0:
            case 's':
                multiplier = 1;
                break;
            case 'm':
                multiplier = 60;
                break;
            case 'h':
                multiplier = 60 * 60;
                break;
            case 'd':
                multiplier = 60 * 60 * 24;
                break;
        }
        if (multiplier == 0) {
            errno = EINVAL;
            return -1;
        }
        d *= multiplier;
    }
    *dp = d;
    return 0;
}

int fsd_format_duration (char *buf, size_t len, double duration)
{
    if (buf == NULL || len <= 0 || is_invalid_duration(duration)) {
        errno = EINVAL;
        return -1;
    }
    if (duration < 60.)
        return snprintf (buf, len, "%gs", duration);
    else if (duration < 60. * 60.)
        return snprintf (buf, len, "%gm", duration / 60.);
    else if (duration < 60. * 60. * 24.)
        return snprintf (buf, len, "%gh", duration / (60. * 60.));
    else
        return snprintf (buf, len, "%gd", duration / (60. * 60. * 24.));
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
