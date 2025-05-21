/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* A count is a wrapper struct containing one of a simple integer,
 * an RFC14 range, or an RFC22 idset.
 */


#ifndef FLUX_COUNT_H
#define FLUX_COUNT_H

#include <limits.h>
#include <stdbool.h>
#include <jansson.h>
#include <flux/idset.h>

#define COUNT_MAX              UINT_MAX
#define COUNT_INVALID_VALUE    IDSET_INVALID_ID

enum count_flags {
    COUNT_FLAG_BRACKETS = IDSET_FLAG_BRACKETS, // encode non-singleton count with brackets
    COUNT_FLAG_SHORT = IDSET_FLAG_RANGE, // encode count in shortened form, if applicable:
                                         // idset with ranges ("2,3,4,8" -> "2-4,8")
                                         // range with defaults omitted ("1-5:1:+" -> "1-5")
};

struct count {
    unsigned int integer;
    unsigned int min;
    unsigned int max;
    unsigned int operand;
    char operator;
    bool isrange;
    struct idset *idset;
};

/* Destroy a count.
 */
void count_destroy (struct count *count);

/* Decode string 's' to a count.
 * Returns count on success, or NULL on failure with errno set.
 */
struct count *count_decode (const char *s);

/* Encode 'count' to a string, which the caller must free.
 * 'flags' may include COUNT_FLAG_BRACKETS, COUNT_FLAG_SHORT.
 * Returns string on success, or NULL on failure with errno set.
 */
char *count_encode (const struct count *count, int flags);

#endif /* !FLUX_COUNT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
