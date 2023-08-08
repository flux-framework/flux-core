/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include <ccan/array_size/array_size.h>
#include <ccan/str/str.h>

#include "parse_size.h"

struct scale {
    const char *s;
    uint64_t scale;
};

static struct scale mtab[] = {
    { "", 1 },
    { "k", 1024 },
    { "K", 1024 }, // upper case K is not the SI prefix but is unambiguous
    { "M", 1024*1024 },
    { "G", 1024UL*1024*1024 },
    { "T", 1024ULL*1024*1024*1024 },
    { "P", 1024ULL*1024*1024*1024*1024 },
    { "E", 1024ULL*1024*1024*1024*1024*1024 },
};

static int lookup_scale (const char *s, uint64_t *vp)
{
    for (int i = 0; i < ARRAY_SIZE (mtab); i++) {
        if (streq (mtab[i].s, s)) {
            *vp = mtab[i].scale;
            return 0;
        }
    }
    return -1;
}

static bool invalid_fp_size (double val)
{
    switch (fpclassify (val)) {
        case FP_NORMAL:     // [[fallthrough]]
        case FP_SUBNORMAL:  // [[fallthrough]]
        case FP_ZERO:       // [[fallthrough]]
            break;          // OK
        case FP_NAN:        // [[fallthrough]]
        case FP_INFINITE:   // [[fallthrough]]
        default:            // something else, bad
            return true;
    }
    if (val < 0.)
        return true;
    return false;
}

static int parse_as_integer (const char *s, uint64_t *up)
{
    char *endptr;
    uint64_t u;
    uint64_t scale;

    // strtoull() allows a leading minus sign but we do not
    if (strchr (s, '-')) {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    u = strtoull (s, &endptr, 0);
    if (errno != 0
        || endptr == s
        || lookup_scale (endptr, &scale) < 0) {
        errno = EINVAL;
        return -1;
    }
    uint64_t result = u * scale;
    if (result < u) {
        errno = EOVERFLOW;
        return -1;
    }
    *up = result;
    return 0;
}

static int parse_as_double (const char *s, uint64_t *up)
{
    char *endptr;
    double d;
    uint64_t scale;

    errno = 0;
    d = strtold (s, &endptr);
    if (errno != 0
        || endptr == s
        || lookup_scale (endptr, &scale) < 0
        || invalid_fp_size (d)) {
        errno = EINVAL;
        return -1;
    }
    double result = floor (d * scale);
    if (result > (double)UINT64_MAX) {
        errno = EOVERFLOW;
        return -1;
    }
    *up = (uint64_t)result;
    return 0;
}

int parse_size (const char *s, uint64_t *vp)
{
    if (!s || !vp) {
        errno = EINVAL;
        return -1;
    }
    if (parse_as_integer (s, vp) < 0
        && parse_as_double (s, vp) < 0)
        return -1;
    return 0;
}

const char *encode_size (uint64_t size)
{
    /* Allocate a thread-local buffer to make this function easy to use
     * in output formats (its intended use case).
     *
     * Note: The maximum printable digits for a double is 15 (DBL_DIG).
     *  We also account for an optional decimal point, suffix, and required
     *  space for NUL to get a buffer size of 18. (We ignore the fact that
     *  a precision is specified in the %g format below for safety).
     */
    static __thread char buf[18];
    const char* suffix[] = {"", "K", "M", "G", "T", "P", "E"};
    int i = 0;
    double value = size;
    while (value >= 1024) {
        value /= 1024;
        i++;
    }
    /* Note: UINT64_MAX is 16E so there is no possibility that 'i' will
     * overflow the suffix array.
     */
    (void) snprintf (buf, sizeof (buf), "%.8g%s", value, suffix[i]);
    return buf;
}

// vi:ts=4 sw=4 expandtab
