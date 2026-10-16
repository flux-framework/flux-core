/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* A resourcecount is a wrapper struct containing one of a simple
 * integer, an RFC14 range, or an RFC22 idset.
 */


#ifndef FLUX_RESOURCECOUNT_H
#define FLUX_RESOURCECOUNT_H

#include <stdbool.h>
#include <jansson.h>
#include <flux/idset.h>

#define RESOURCECOUNT_INVALID_VALUE    IDSET_INVALID_ID
#define RESOURCECOUNT_MAX              (IDSET_INVALID_ID - 1)

enum resourcecount_flags {
    RESOURCECOUNT_FLAG_BRACKETS =
        IDSET_FLAG_BRACKETS,  // encode non-singleton resourcecount with brackets
    RESOURCECOUNT_FLAG_SHORT =
        IDSET_FLAG_RANGE,  // encode resourcecount in shortened form, if applicable:
                           // idset with ranges ("2,3,4,8" -> "2-4,8")
                           // range with defaults omitted ("1-5:1:+" -> "1-5")
};

/* Create a resourcecount from a json object.
 * Returns resourcecount on success, or NULL on failure with error->text set.
 */
struct resourcecount *resourcecount_create (json_t *json_count, json_error_t *error);

/* Destroy a resourcecount.
 */
void resourcecount_destroy (struct resourcecount *count);

/* Decode string 's' to a resourcecount.
 * Returns resourcecount on success, or NULL on failure with errno set.
 */
struct resourcecount *resourcecount_decode (const char *s);

/* Encode 'resourcecount' to a string, which the caller must free.
 * 'flags' may include RESOURCECOUNT_FLAG_BRACKETS or RESOURCECOUNT_FLAG_SHORT.
 * Returns string on success, or NULL on failure with errno set.
 */
char *resourcecount_encode (const struct resourcecount *count, int flags);

/* Return the first value in the resourcecount.
 * Returns RESOURCECOUNT_INVALID_VALUE if count is NULL.
 */
unsigned int resourcecount_first (const struct resourcecount *count);

/* Return the next value in the resourcecount.
 * Returns RESOURCECOUNT_INVALID_VALUE if 'value' is the last valid value,
 * or if count is NULL or a simple integer (as there is no "next" value).
 * N.B. if count->isrange, this does not check if 'value' is a valid value;
 * it should only be called using valid values produced by prior calls to
 * resourcecount_first or resourcecount_next.
 */
unsigned int resourcecount_next (const struct resourcecount *count, unsigned int value);

/* Returns true if count is a simple integer, i.e. count->integer > 0.
 * Returns false otherwise, or if count is NULL.
 */
bool resourcecount_is_integer (const struct resourcecount *count);

/* Returns true if count is an unbounded range.
 * Returns false otherwise, or if count is NULL.
 */
bool resourcecount_is_unbounded (const struct resourcecount *count);

#endif /* !FLUX_RESOURCECOUNT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
