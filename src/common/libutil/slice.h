/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_SLICE_H
#define _UTIL_SLICE_H

struct slice {
    int start;
    int stop;
    int step;

    size_t length;
    int cursor;
};

/* Parse 's' as a python style array slice, e.g. [start:stop:step].
 * 'array_length' is the length of the array to be sliced.
 * Returns 0 on success, -1 on failure.  Errno is undefined on failure.
 */
int slice_parse (struct slice *sl, const char *s, size_t array_length);

/* Built in iterator returns zero-origin sliced array indices (-1 at end).
 */
int slice_first (struct slice *sl);
int slice_next (struct slice *sl);

#endif /* !_UTIL_SLICE_H */

// vi:ts=4 sw=4 expandtab
