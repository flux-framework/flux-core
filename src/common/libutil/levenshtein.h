/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_LEVENSHTEIN_H
#define _UTIL_LEVENSHTEIN_H

#include <stddef.h>

/*  Calculate the Levenshtein distance between two strings.
 *
 *  The Levenshtein distance is the minimum number of single-character
 *  operations (insertions, deletions, or substitutions) required to
 *  change one string into another.
 *
 *  Parameters:
 *    s1 - First string
 *    s2 - Second string
 *
 *  Returns:
 *    The Levenshtein distance between the two strings.
 *    Returns -1 on error (e.g., memory allocation failure).
 */
int levenshtein_distance (const char *s1, const char *s2);

#endif /* !_UTIL_LEVENSHTEIN_H */

// vi:ts=4 sw=4 expandtab