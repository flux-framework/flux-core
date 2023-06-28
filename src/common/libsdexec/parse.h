/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _LIBSDEXEC_PARSE_H
#define _LIBSDEXEC_PARSE_H

#include <sys/types.h>
#include <stdint.h>

/* Parse 's' as a percentage between 0 and 100 with % suffix and
 * assign the result, a 0 <= value <= 1.0, to 'dp'.
 * Return 0 on success or -1 on failure.  Errno is not set.
 */
int sdexec_parse_percent (const char *s, double *dp);

/* Parse 's' as an idset into bitmap assigned to 'bp' (caller must free).
 * The size of the bitmap (in bytes) is assigned to 'sp'.
 * N.B. an empty string translates to an empty (*bp=NULL, *sp=0) bitmap.
 * Return 0 on success or -1 on failure.  Errno is not set.
 */
int sdexec_parse_bitmap (const char *s, uint8_t **bp, size_t *sp);

#endif /* !_LIBSDEXEC_PARSE_H */

// vi:ts=4 sw=4 expandtab
