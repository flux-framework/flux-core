/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "parse.h"

int parse_percent (const char *s, double *percent)
{
    double p;
    int len;
    char *endptr;

    if (!s || !percent) {
        errno = EINVAL;
        return -1;
    }

    /* infinity = 100% */
    if (!strcasecmp (s, "infinity")) {
        (*percent) = 1.0;
        return 0;
    }

    len = strlen (s);
    if (s[len - 1] != '%') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    p = strtod (s, &endptr);
    if (errno != 0)
        return -1;
    if (endptr != &s[len -1]
        || p < 0.0
        || p > 100.0) {
        errno = EINVAL;
        return -1;
    }

    (*percent) = (p / 100.0);
    return 0;
}

int parse_unsigned (const char *s, uint64_t *num)
{
    uint64_t n;
    char *endptr;

    if (!s || !num || s[0] == '-') {
        errno = EINVAL;
        return -1;
    }

    errno = 0;
    n = strtoull (s, &endptr, 0);
    if (errno != 0)
        return -1;
    if (n == 0) {
        errno = EINVAL;
        return -1;
    }
    if ((*endptr) != '\0') {
        uint64_t mult = 1;
        if (strlen (endptr) > 1) {
            errno = EINVAL;
            return -1;
        }
        if ((*endptr) == 'k'
            || (*endptr) == 'K')
            mult = 1024ULL;
        else if ((*endptr) == 'm'
                 || (*endptr) == 'M')
            mult = 1024ULL * 1024ULL;
        else if ((*endptr) == 'g'
                 || (*endptr) == 'G')
            mult = 1024ULL * 1024ULL * 1024ULL;
        else if ((*endptr) == 't'
                 || (*endptr) == 'T')
            mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        else {
            errno = EINVAL;
            return -1;
        }
        if ((ULLONG_MAX / mult) < n) {
            errno = ERANGE;
            return -1;
        }
        n *= mult;
    }

    (*num) = n;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
