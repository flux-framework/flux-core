/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _UTIL_BASEMOJI_H
#define _UTIL_BASEMOJI_H

#include <stdint.h>
#include <stdbool.h>

/*  basemoji - an implementation the RFC 19 FLUID emoji encoding
 */

/* Convert a 64 bit unsigned integer to basemoji, placing the result
 * in buffer 'buf' of size 'buflen'.
 *
 * Returns 0 on success, -1 on failure with errno set:
 * EINVAL: Invalid arguments
 * EOVERFLOW: buffer too small for encoded string
 */
int uint64_basemoji_encode (uint64_t id, char *buf, int buflen);

/* Decode a string in basemoji to an unsigned 64 bit integer.
 *
 * Returns 0 on success, -1 on failure with errno set:
 * EINVAL: Invalid arguments
 */
int uint64_basemoji_decode (const char *str, uint64_t *idp);

/*  Return true if 's' could be a basemoji string, i.e. it falls
 *  within the minimum and maximum lengths, and starts with the
 *  expected bytes.
 */
bool is_basemoji_string (const char *s);

#endif /* !_UTIL_BASEMOJI_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
