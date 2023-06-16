/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_PARSE_SIZE_H
#define _UTIL_PARSE_SIZE_H

#include <stdint.h>

/* Parse 's' as a floating point quantity scaled by optional suffix:
 *
 *  k,K   2^10 (1024)
 *  M     2^20
 *  G     2^30
 *  T     2^40
 *  P     2^50
 *  E     2^60
 *
 * The numeric part is parsed with first strtoull(3) then strtod(3),
 * so all input supported by those functions should work including
 * decimal (255), hex (0xf), octal (0377 prefix), exponent (2.55E2), etc.
 *
 * Assign the result to 'vp' and return 0 on success,
 * or return -1 on failure with errno set (EINVAL, EOVERFLOW).
 */
int parse_size (const char *s, uint64_t *vp);

/* Format 'size' as a human readable string using suffixes documented
 * above for parse_size(). Note that due to use of double precision
 * arithmetic and because the result is rounded to 8 significant figures
 * the returned string may be imprecise. Passing the result of encode_size()
 * to parse_size() may not result in the same value for 'size'.
 *
 * The result is only good until the next call to encode_size() from the
 * current thread.
 */
const char *encode_size (uint64_t size);

#endif /* !_UTIL_PARSE_SIZE_H */

// vi:ts=4 sw=4 expandtab
