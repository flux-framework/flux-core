/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_JSONLIMIT_H
#define _UTIL_JSONLIMIT_H

#include <stddef.h>
#include <jansson.h>

#include "src/common/libflux/types.h" /* flux_error_t */

/*  Default limits for JSON objects that enter the system from an
 *  untrusted or semi-trusted source (job manager plugins, user requests)
 *  and may be written to an eventlog or forwarded on the journal.
 *
 *  JSON_LIMIT_MAX_DEPTH is the maximum nesting depth. The value is chosen
 *  to match the most restrictive mainstream JSON parser likely to consume
 *  this data, since eventlog and journal entries are deserialized by
 *  consumers written in C, Python, and Rust:
 *
 *    serde_json (Rust)  128   (Deserializer::new: remaining_depth = 128)
 *    python json        ~497  (pure python scanner, recursionlimit 1000)
 *    jansson (C)        2048  (JSON_PARSER_MAX_DEPTH, parsing only)
 *
 *  A limit of 128 is therefore safe for all consumers, while remaining far
 *  above the depth of any legitimate jobspec or annotation object.
 *
 *  JSON_LIMIT_MAX_SIZE is the maximum size in bytes of the object when
 *  serialized with JSON_COMPACT. This is a backstop against unbounded
 *  growth rather than a tight bound, so it is set to a reasonable limit
 *  of 1MiB. This limit is meant to apply to journal updates, etc., and
 *  does not necessarily apply to entire jobspecs. (A different limit could
 *  be applied there by passing a custom max_size directly to 
 *  json_check_limits()).
 */
#define JSON_LIMIT_MAX_DEPTH    128
#define JSON_LIMIT_MAX_SIZE     1048576

/*  Check that 'o' does not nest more than 'max_depth' levels deep and does
 *  not exceed 'max_size' bytes when serialized compactly.
 *
 *  Either limit may be 0 to skip that particular check.
 *
 *  A NULL 'o' is not an error, so that the result of a lookup for an
 *  optional value may be passed directly.
 *
 *  The depth check is performed first and exits early, so a pathological
 *  object is rejected without being serialized.
 *
 *  Returns 0 on success, or -1 on failure with errno set to EINVAL and
 *  'errp' (if non-NULL) filled in with the reason for the failure.
 */
int json_check_limits (json_t *o,
                       int max_depth,
                       size_t max_size,
                       flux_error_t *errp);

/*  Convenience wrapper to check 'o' against the default limits above.
 */
static inline int json_check_default_limits (json_t *o, flux_error_t *errp)
{
    return json_check_limits (o,
                              JSON_LIMIT_MAX_DEPTH,
                              JSON_LIMIT_MAX_SIZE,
                              errp);
}

#endif /* !_UTIL_JSONLIMIT_H */

// vi:ts=4 sw=4 expandtab
