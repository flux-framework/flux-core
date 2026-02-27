/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "levenshtein.h"

/* Return the minimum of three values */
static inline int min3 (int a, int b, int c)
{
    int min = a;
    if (b < min)
        min = b;
    if (c < min)
        min = c;
    return min;
}

int levenshtein_distance (const char *s1, const char *s2)
{
    int *matrix;
    int s1len, s2len;
    int i, j;
    int result;

    /* Handle NULL inputs */
    if (!s1 || !s2) {
        errno = EINVAL;
        return -1;
    }

    /* Get string lengths */
    s1len = strlen(s1);
    s2len = strlen(s2);

    /* Fast path for empty strings */
    if (s1len == 0)
        return s2len;
    if (s2len == 0)
        return s1len;

    /* Allocate a single-dimensional matrix to represent a 2D array
     * of size (s1len+1) x (s2len+1) */
    matrix = malloc ((s1len + 1) * (s2len + 1) * sizeof (int));
    if (!matrix) {
        errno = ENOMEM;
        return -1;
    }

    /* Initialize first row */
    for (i = 0; i <= s1len; i++)
        matrix[i * (s2len + 1)] = i;

    /* Initialize first column */
    for (j = 0; j <= s2len; j++)
        matrix[j] = j;

    /* Fill in the rest of the matrix */
    for (i = 1; i <= s1len; i++) {
        for (j = 1; j <= s2len; j++) {
            int cost = (s1[i-1] == s2[j-1]) ? 0 : 1;

            matrix[i * (s2len + 1) + j] = min3(
                matrix[(i-1) * (s2len + 1) + j] + 1,     /* deletion */
                matrix[i * (s2len + 1) + (j-1)] + 1,     /* insertion */
                matrix[(i-1) * (s2len + 1) + (j-1)] + cost  /* substitution */
            );
        }
    }

    /* Get the result from the bottom-right cell */
    result = matrix[s1len * (s2len + 1) + s2len];

    free (matrix);
    return result;
}

/* vi: ts=4 sw=4 expandtab
 */
