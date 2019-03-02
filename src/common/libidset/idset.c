/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
    idset->count = 0;
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
    cpy->count = idset->count;
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

/* Wrapper for vebput() which increments idset count if needed
 */
static void idset_put (struct idset *idset, unsigned int id)
{
    if (!idset_test (idset, id))
        idset->count++;
    vebput (idset->T, id);
}

/* Wrapper for vebdel() which decrements idset count if needed
 */
static void idset_del (struct idset *idset, unsigned int id)
{
    if (idset_test (idset, id))
        idset->count--;
    vebdel (idset->T, id);
}

int idset_set (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    if (idset_grow (idset, id + 1) < 0)
        return -1;
    idset_put (idset, id);
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
        idset_put (idset, id);
    return 0;
}

int idset_clear (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    idset_del (idset, id);
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
        idset_del (idset, id);
    return 0;
}

bool idset_test (const struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id) || id >= idset->T.M)
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

unsigned int idset_last (const struct idset *idset)
{
    unsigned int last = IDSET_INVALID_ID;

    if (idset) {
        last = vebpred (idset->T, idset->T.M - 1);
        if (last == idset->T.M)
            last = IDSET_INVALID_ID;
    }
    return last;
}

size_t idset_count (const struct idset *idset)
{
    if (!idset)
        return 0;
    return idset->count;
}

bool idset_equal (const struct idset *idset1,
                  const struct idset *idset2)
{
    unsigned int id;

    if (!idset1 || !idset2)
        return false;
    if (idset_count (idset1) != idset_count (idset2))
        return false;

    id = vebsucc (idset1->T, 0);
    while (id < idset1->T.M) {
        if (vebsucc (idset2->T, id) != id)
            return false; // id in idset1 not set in idset2
        id = vebsucc (idset1->T, id + 1);
    }
    id = vebsucc (idset2->T, 0);
    while (id < idset2->T.M) {
        if (vebsucc (idset1->T, id) != id)
            return false; // id in idset2 not set in idset1
        id = vebsucc (idset2->T, id + 1);
    }
    return true;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
