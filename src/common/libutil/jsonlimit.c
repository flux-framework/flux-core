/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* jsonlimit.c - bound the depth and size of untrusted JSON
 *
 * A JSON object that is accepted by the job manager may be written to a
 * job eventlog and forwarded to every journal consumer. Since jansson
 * places no limit on the depth of an object it serializes, but every JSON
 * parser places a limit on the depth of an object it will parse, it is
 * possible to emit data that no consumer can read back. See
 * flux-framework/flux-core#7815, where a plugin repeatedly wrapped the
 * jobspec constraints in a new {"and": [...]} object until the job-list
 * module could no longer deserialize the result and exited.
 *
 * Bounding depth and size where the data enters the system avoids this.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"

#include "jsonlimit.h"

/*  Return true if 'o' nests deeper than 'max_depth' levels.
 *
 *  Recursion is bounded by 'max_depth' since the walk stops descending as
 *  soon as the limit is exceeded, so this cannot itself overflow the stack
 *  for any sane limit.
 */
static bool exceeds_depth (json_t *o, int max_depth)
{
    size_t index;
    const char *key;
    json_t *value;

    /*  Scalars terminate the walk. Only arrays and objects add depth, so
     *  this is also the fast path for the common case of a small, flat
     *  object containing only scalar values.
     */
    if (!json_is_array (o) && !json_is_object (o))
        return false;
    if (max_depth <= 0)
        return true;

    if (json_is_array (o)) {
        json_array_foreach (o, index, value) {
            if (exceeds_depth (value, max_depth - 1))
                return true;
        }
    }
    else {
        json_object_foreach (o, key, value) {
            if (exceeds_depth (value, max_depth - 1))
                return true;
        }
    }
    return false;
}

int json_check_limits (json_t *o,
                       int max_depth,
                       size_t max_size,
                       flux_error_t *errp)
{
    size_t size;

    /*  Nothing to check. This is not an error so that a caller may pass
     *  the result of a lookup for an optional value directly.
     */
    if (!o)
        return 0;

    /*  Check depth before size so that a pathologically nested object is
     *  rejected without being serialized.
     */
    if (max_depth > 0 && exceeds_depth (o, max_depth)) {
        errno = EINVAL;
        return errprintf (errp,
                          "exceeds maximum nesting depth of %d",
                          max_depth);
    }
    if (max_size > 0) {
        /*  json_dumpb() with a NULL buffer returns the serialized length
         *  without producing the string. (It is not allocation free: for
         *  cycle detection jansson adds each container node to a hash
         *  table as it walks.)
         *
         *  JSON_ENCODE_ANY is required so that a bare scalar, which is not
         *  a valid JSON document on its own, still reports a length rather
         *  than 0.
         */
        size = json_dumpb (o, NULL, 0, JSON_COMPACT | JSON_ENCODE_ANY);
        if (size == 0) {
            errno = EINVAL;
            return errprintf (errp, "object could not be serialized");
        }
        if (size > max_size) {
            errno = EINVAL;
            return errprintf (errp,
                              "size of %zu bytes exceeds maximum of %zu",
                              size,
                              max_size);
        }
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
