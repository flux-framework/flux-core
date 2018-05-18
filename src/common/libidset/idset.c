/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#include "idset.h"
#include "idset_private.h"

int validate_idset_flags (int flags, int allowed)
{
    if ((flags & allowed) != flags) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

struct idset *idset_create (size_t size, int flags)
{
    struct idset *idset;

    if (validate_idset_flags (flags, IDSET_FLAG_AUTOGROW) < 0)
        return NULL;
    if (size == 0)
        size = IDSET_DEFAULT_SIZE;
    if (!(idset = malloc (sizeof (*idset))))
        return NULL;
    idset->T = vebnew (size, 0);
    if (!idset->T.D) {
        free (idset);
        errno = ENOMEM;
        return NULL;
    }
    idset->flags = flags;
    return idset;
}

void idset_destroy (struct idset *idset)
{
    if (idset) {
        int saved_errno = errno;
        free (idset->T.D);
        free (idset);
        errno = saved_errno;
    }
}

static Veb vebdup (Veb T)
{
    size_t size = vebsize (T.M);
    Veb cpy;

    cpy.k = T.k;
    cpy.M = T.M;
    if ((cpy.D = malloc (size)))
        memcpy (cpy.D, T.D, size);
    return cpy;
}

struct idset *idset_copy (const struct idset *idset)
{
    struct idset *cpy;

    if (!idset) {
        errno = EINVAL;
        return NULL;
    }
    if (!(cpy = malloc (sizeof (*idset))))
        return NULL;
    cpy->flags = idset->flags;
    cpy->T = vebdup (idset->T);
    if (!cpy->T.D) {
        idset_destroy (cpy);
        return NULL;
    }
    return cpy;
}

static bool valid_id (unsigned int id)
{
    if (id == UINT_MAX || id == IDSET_INVALID_ID)
        return false;
    return true;
}

/* Double idset size until it has at least 'size' slots.
 * Return 0 on success, -1 on failure with errno == ENOMEM.
 */
static int idset_grow (struct idset *idset, size_t size)
{
    size_t newsize = idset->T.M;
    Veb T;
    unsigned int id;

    while (newsize < size)
        newsize <<= 1;

    if (newsize > idset->T.M) {
        if (!(idset->flags & IDSET_FLAG_AUTOGROW)) {
            errno = EINVAL;
            return -1;
        }
        T = vebnew (newsize, 0);
        if (!T.D)
            return -1;

        id = vebsucc (idset->T, 0);
        while (id < idset->T.M) {
            vebput (T, id);
            id = vebsucc (idset->T, id + 1);
        }
        free (idset->T.D);
        idset->T = T;
    }
    return 0;
}

int idset_set (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    if (idset_grow (idset, id + 1) < 0)
        return -1;
    vebput (idset->T, id);
    return 0;
}

static void normalize_range (unsigned int *lo, unsigned int *hi)
{
    if (*hi < *lo) {
        unsigned int tmp = *hi;
        *hi = *lo;
        *lo = tmp;
    }
}

int idset_range_set (struct idset *idset, unsigned int lo, unsigned int hi)
{
    unsigned int id;

    if (!idset || !valid_id (lo) || !valid_id (hi)) {
        errno = EINVAL;
        return -1;
    }
    normalize_range (&lo, &hi);
    if (idset_grow (idset, hi + 1) < 0)
        return -1;
    for (id = lo; id <= hi; id++)
        vebput (idset->T, id);
    return 0;
}

int idset_clear (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    vebdel (idset->T, id);
    return 0;
}

int idset_range_clear (struct idset *idset, unsigned int lo, unsigned int hi)
{
    unsigned int id;

    if (!idset || !valid_id (lo) || !valid_id (hi)) {
        errno = EINVAL;
        return -1;
    }
    normalize_range (&lo, &hi);
    for (id = lo; id <= hi && id < idset->T.M; id++)
        vebdel (idset->T, id);
    return 0;
}

bool idset_test (const struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id))
        return false;
    return (vebsucc (idset->T, id) == id);
}

unsigned int idset_first (const struct idset *idset)
{
    unsigned int next = IDSET_INVALID_ID;

    if (idset) {
        next = vebsucc (idset->T, 0);
        if (next == idset->T.M)
            next = IDSET_INVALID_ID;
    }
    return next;
}


unsigned int idset_next (const struct idset *idset, unsigned int prev)
{
    unsigned int next = IDSET_INVALID_ID;

    if (idset) {
        next = vebsucc (idset->T, prev + 1);
        if (next == idset->T.M)
            next = IDSET_INVALID_ID;
    }
    return next;
}

size_t idset_count (const struct idset *idset)
{
    unsigned int id;
    size_t count = 0;

    if (!idset)
        return 0;
    id = vebsucc (idset->T, 0);
    while (id < idset->T.M) {
        count++;
        id = vebsucc (idset->T, id + 1);
    }
    return count;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
