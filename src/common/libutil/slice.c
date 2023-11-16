/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* slice.c - python style array slicing
 *
 * https://python-reference.readthedocs.io/en/latest/docs/brackets/slicing.html
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

#include "slice.h"

static int parse_enclosing (char **cp, char begin, char end)
{
    size_t slen = strlen (*cp);

    if (slen < 2)
        return -1;
    if ((*cp)[0] != begin || (*cp)[slen - 1] != end)
        return -1;
    (*cp)[slen - 1] = '\0';
    (*cp)++;
    return 0;
}

// returns 1 if value was provided, 0 if default was assigned, -1 if error
static int parse_int (char **cp, int def, char sep, int *value)
{
    int v;
    char *endptr;

    errno = 0;
    v = strtol (*cp, &endptr, 10);
    // standard says EINVAL may be set if zero is returned for nothing parsed
    if (v != 0 && errno != 0)
        return -1;
    if (endptr == *cp) {    // no digits
        if ((*cp)[0] == sep)
            (*cp)++;
        else if ((*cp)[0] != '\0')
            return -1;
        *value = def;
        return 0;
    }
    if (*endptr == '\0') {  // entire string consumed
        *cp = endptr;
        *value = v;
        return 1;
    }
    if (*endptr == sep) {   // string consumed to separator
        *cp = endptr + 1;
        *value = v;
        return 1;
    }
    return -1;
}

// return true if index has surpassed bounds of array or slice
static bool overrun (struct slice *sl, int i)
{
    if (sl->step > 0 && (i >= sl->stop || i >= sl->length))
        return true;
    if (sl->step < 0 && (i <= sl->stop || i < 0))
        return true;
    return false;
}

// set cursor to first slice index that's in array bounds
static void cursor_first (struct slice *sl)
{
    sl->cursor = sl->start;
    while (!overrun (sl, sl->cursor)) {
        if (sl->cursor >= 0 && sl->cursor < sl->length)
            return;
        sl->cursor += sl->step;
    }
    sl->cursor = -1;
}

// set cursor to next slice index
static void cursor_next (struct slice *sl)
{
    if (sl->cursor != -1) {
        do {
            sl->cursor += sl->step;
            if (overrun (sl, sl->cursor)) {
                sl->cursor = -1;
                return;
            }
        } while (sl->cursor < 0 || sl->cursor >= sl->length);
    }
}

int slice_first (struct slice *sl)
{
    if (!sl)
        return -1;
    cursor_first (sl);
    return slice_next (sl);
}

int slice_next (struct slice *sl)
{
    if (!sl)
        return -1;
    int i = sl->cursor;
    cursor_next (sl);
    return i;
}

int slice_parse (struct slice *sl, const char *s, size_t array_length)
{
    char *cpy;
    char *cp;
    int rc1, rc2;

    if (!sl || !s || !strchr (s, ':'))
        return -1;
    if (!(cpy = strdup (s)))
        return -1;
    sl->length = array_length;
    cp = cpy;
    if (parse_enclosing (&cp, '[', ']') < 0)
        goto error;
    if ((rc1 = parse_int (&cp, 0, ':', &sl->start)) < 0)
        goto error;
    if ((rc2 = parse_int (&cp, array_length, ':', &sl->stop)) < 0)
        goto error;
    if (parse_int (&cp, 1, ':', &sl->step) < 0)
        goto error;
    if (sl->step == 0)
        goto error;
    // transform negative indices to positive
    if (sl->start < 0)
        sl->start = sl->length + sl->start;
    if (sl->stop < 0)
        sl->stop = sl->length + sl->stop;
    // fix up default step/stop assigned above if step is negative
    if (sl->step < 0) {
        if (rc1 == 0)
            sl->start = array_length - 1;
        if (rc2 == 0)
            sl->stop = -1;
    }
    cursor_first (sl);
    free (cpy);
    return 0;
error:
    free (cpy);
    return -1;
}

// vi:ts=4 sw=4 expandtab
