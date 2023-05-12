/************************************************************\
 * Copyright 2016 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef UTIL_BASE256_H
#define UTIL_BASE256_H

#include <stdbool.h>

#define BASE256_ENCODED_SIZE(x) ((x * 4) + 4 + 1)
#define BASE256_DECODED_SIZE(x) ((x / 4) - 4 - 1)
#define BASE64_PREFIX "🇫"
#define is_base256(x) (strlen((x)) > 4 && memcmp ((x), BASE64_PREFIX, 4) == 0)

#ifdef __cplusplus
extern "C" {
#endif

/*  Decode string `data` in base256 into result `buf` of size `buflen`,
 *  which should be at least size BASE256_DECODED_SIZE(strlen(in))
 *
 *  Returns 0 on success, -1 on error with errno set:
 *  EINVAL: invalid arguments
 *  ENOENT: an invalid character appears in data
 */
extern int base256_decode (void *buf,
                           int buflen,
                           const char *data);

/*  Encode `datalen` bytes in `data` to base256, placing results in
 *  buffer `buf` of size `buflen` (should be at least size
 *  BASE256_ENCODED_SIZE (datalen)).
 *
 *  Returns 0 on success, -1 on error with errno set:
 *  EINVAL: invalid arguments
 */
extern int base256_encode (char *buf,
                           int buflen,
                           void *data,
                           int datalen);

#ifdef __cplusplus
}
#endif

#endif /* UTILBASE256_H */
