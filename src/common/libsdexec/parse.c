/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
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

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <flux/idset.h>

#include "ccan/str/str.h"
#include "parse.h"

int sdexec_parse_percent (const char *s, double *dp)
{
    char *endptr;
    double d;

    if (!s || !dp)
        return -1;
    errno = 0;
    d = strtod (s, &endptr);
    if (errno != 0 || endptr == s || strlen (endptr) != 1 || endptr[0] != '%')
        return -1;
    if (d < 0 || d > 100)
        return -1;
    *dp = d * 1E-2;
    return 0;
}

/* Borrow some macro ideas from ccan/bitmap/bitmap.h.  It cannot be used here
 * because it apparently swaps bytes of internal words on little endian for
 * portability, which isn't compatible with the byte array requirement of
 * systemd/dbus.
 */
#define BITMAP_NBYTES(nb)           (((nb) + 8 - 1) / 8)
#define BITMAP_BYTE(bm,n)           (bm)[(n) / 8]
#define BITMAP_BIT(n)               (1 << ((n) % 8))

int sdexec_parse_bitmap (const char *s, uint8_t **bp, size_t *sp)
{
    struct idset *ids;
    unsigned int id;
    unsigned int nbits = 0;
    uint8_t *bitmap = NULL;

    if (!s || !bp || !sp)
        return -1;
    if (!(ids = idset_decode (s)))
        return -1;

    // allocate a bitmap large enough to fit idset
    if ((id = idset_last (ids)) != IDSET_INVALID_ID)
        nbits = id + 1;
    if (nbits > 0) {
        if (!(bitmap = calloc (1, BITMAP_NBYTES (nbits)))) {
            idset_destroy (ids);
            return -1;
        }
        id = idset_first (ids);
        while (id != IDSET_INVALID_ID) {
            BITMAP_BYTE (bitmap, id) |= BITMAP_BIT (id);
            id = idset_next (ids, id);
        }
        idset_destroy (ids);
    }
    *bp = bitmap;
    *sp = BITMAP_NBYTES (nbits);
    return 0;
}

// vi:ts=4 sw=4 expandtab
