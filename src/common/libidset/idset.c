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
    int valid_flags = IDSET_FLAG_AUTOGROW
                    | IDSET_FLAG_INITFULL
                    | IDSET_FLAG_COUNT_LAZY
                    | IDSET_FLAG_ALLOC_RR;

    if (validate_idset_flags (flags, valid_flags) < 0)
        return NULL;
    if (size == 0)
        size = IDSET_DEFAULT_SIZE;
    if (!(idset = malloc (sizeof (*idset))))
        return NULL;
    if ((flags & IDSET_FLAG_INITFULL))
        idset->T = vebnew (size, 1);
    else
        idset->T = vebnew (size, 0);
    if (!idset->T.D) {
        free (idset);
        errno = ENOMEM;
        return NULL;
    }
    idset->flags = flags;
    if ((flags & IDSET_FLAG_INITFULL))
        idset->count = size;
    else
        idset->count = 0;
    if ((flags & IDSET_FLAG_ALLOC_RR))
        idset->alloc_rr_last = IDSET_INVALID_ID;
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

size_t idset_universe_size (const struct idset *idset)
{
    return idset ? idset->T.M : 0;
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

static struct idset *idset_copy_flags (const struct idset *idset, int flags)
{
    struct idset *cpy;

    if (!(cpy = malloc (sizeof (*idset))))
        return NULL;
    cpy->flags = flags;
    cpy->T = vebdup (idset->T);
    if (!cpy->T.D) {
        idset_destroy (cpy);
        return NULL;
    }
    cpy->count = idset->count;
    return cpy;
}

struct idset *idset_copy (const struct idset *idset)
{
    if (!idset) {
        errno = EINVAL;
        return NULL;
    }
    return idset_copy_flags (idset, idset->flags);
}

static bool valid_id (unsigned int id)
{
    if (id == UINT_MAX || id == IDSET_INVALID_ID)
        return false;
    return true;
}

/* Double the idset universe size until it is at least 'size'.
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
        if ((idset->flags & IDSET_FLAG_INITFULL)) {
            for (id = idset->T.M; id < newsize; id++)
                vebput (T, id);
            idset->count += (newsize - idset->T.M);
        }
        free (idset->T.D);
        idset->T = T;
    }
    return 0;
}

/* Helper to avoid costly idset_test() operation in idset_put()/idset_del()
 * in some cases that may commonly arise in idset_encode(), for example.
 * This function runs in constant time.  Return true if id is definitely not
 * in set.  A false result is indeterminate.
 */
static bool nonmember_fast (struct idset *idset, unsigned int id)
{
    unsigned int last = idset_last (idset);
    if (last == IDSET_INVALID_ID || id > last)
        return true;
    unsigned int first = idset_first (idset);
    if (first == IDSET_INVALID_ID || id < first)
        return true;
    return false;
}

/* Wrapper for vebput() which increments idset count.
 * The operation is skipped if id is already in the set.
 */
static void idset_put (struct idset *idset, unsigned int id)
{
    if ((idset->flags & IDSET_FLAG_COUNT_LAZY)
        || nonmember_fast (idset, id)
        || !idset_test (idset, id)) {
        idset->count++;
        vebput (idset->T, id);
    }
}

/* Call this variant if id is known to NOT be in the set
 */
static void idset_put_nocheck (struct idset *idset, unsigned int id)
{
    idset->count++;
    vebput (idset->T, id);
}

/* Wrapper for vebdel() which decrements idset count.
 * The operation is skipped if id is not in the set.
 */
static void idset_del (struct idset *idset, unsigned int id)
{
    if ((idset->flags & IDSET_FLAG_COUNT_LAZY)
        || (!nonmember_fast (idset, id) && idset_test (idset, id))) {
        idset->count--;
        vebdel (idset->T, id);
    }
}

/* Call this variant if id is known to be IN the set
 */
static void idset_del_nocheck (struct idset *idset, unsigned int id)
{
    idset->count--;
    vebdel (idset->T, id);
}

int idset_set (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    if (id >= idset_universe_size (idset)) {
        /* N.B. we do not try to grow the idset to accommodate out of range ids
         * when the operation is 'set' and IDSET_FLAG_INITFULL is set.
         * Treat it as a successful no-op.
         */
        if ((idset->flags & IDSET_FLAG_INITFULL))
            return 0;
        if (idset_grow (idset, id + 1) < 0)
            return -1;
        idset_put_nocheck (idset, id);
    }
    else
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

    // see IDSET_FLAG_INITFULL note in idset_set()
    size_t oldsize = idset_universe_size (idset);
    if (!(idset->flags & IDSET_FLAG_INITFULL)) {
        if (idset_grow (idset, hi + 1) < 0)
            return -1;
    }
    for (id = lo; id <= hi; id++) {
        if (id >= oldsize) {
            if ((idset->flags & IDSET_FLAG_INITFULL))
                return 0;
            idset_put_nocheck (idset, id);
        }
        else
            idset_put (idset, id);
    }
    return 0;
}

int idset_clear (struct idset *idset, unsigned int id)
{
    if (!idset || !valid_id (id)) {
        errno = EINVAL;
        return -1;
    }
    if (id >= idset_universe_size (idset)) {
        /* N.B. we do not try to grow the idset to accommodate out of range ids
         * when the operation is 'clear' and IDSET_FLAG_INITFULL is NOT set.
         * Treat this as a successful no-op.
         */
        if (!(idset->flags & IDSET_FLAG_INITFULL))
            return 0;
        if (idset_grow (idset, id + 1) < 0)
            return -1;
        idset_del_nocheck (idset, id);
    }
    else
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
    // see IDSET_FLAG_INITFULL note in idset_clear()
    size_t oldsize = idset_universe_size (idset);
    if ((idset->flags & IDSET_FLAG_INITFULL)) {
        if (idset_grow (idset, hi + 1) < 0)
            return -1;
    }
    for (id = lo; id <= hi; id++) {
        if (id >= oldsize) {
            if (!(idset->flags & IDSET_FLAG_INITFULL))
                return 0;
            idset_del_nocheck (idset, id);
        }
        else
            idset_del (idset, id);
    }
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


unsigned int idset_next (const struct idset *idset, unsigned int id)
{
    unsigned int next = IDSET_INVALID_ID;

    if (idset) {
        next = vebsucc (idset->T, id + 1);
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

unsigned int idset_prev (const struct idset *idset, unsigned int id)
{
    unsigned int next = IDSET_INVALID_ID;

    if (idset) {
        next = vebpred (idset->T, id - 1);
        if (next == idset->T.M)
            next = IDSET_INVALID_ID;
    }
    return next;
}

size_t idset_count (const struct idset *idset)
{
    if (!idset)
        return 0;
    if (!(idset->flags & IDSET_FLAG_COUNT_LAZY))
        return idset->count;

    /* IDSET_FLAG_COUNT_LAZY was set, causing set/clear operations to ignore
     * safeguards that kept idset->count accurate.  Pay now by iterating.
     */
    unsigned int id;
    size_t count = 0;

    id = idset_first (idset);
    while (id != IDSET_INVALID_ID) {
        count++;
        id = idset_next (idset, id);
    }
    return count;
}

bool idset_empty (const struct idset *idset)
{
    if (!idset || vebsucc (idset->T, 0) == idset->T.M)
        return true;
    return false;
}

bool idset_equal (const struct idset *idset1,
                  const struct idset *idset2)
{
    unsigned int id;
    bool count_checked = false;

    if (!idset1 || !idset2)
        return false;

    /* As an optimization, declare the sets unequal if counts differ.
     * If lazy counts are used, this is potentially slow, so skip.
     */
    if (!(idset1->flags & IDSET_FLAG_COUNT_LAZY)
        && !(idset2->flags & IDSET_FLAG_COUNT_LAZY)) {
        if (idset_count (idset1) != idset_count (idset2))
            return false;
        count_checked = true;
    }

    id = vebsucc (idset1->T, 0);
    while (id < idset1->T.M) {
        if (vebsucc (idset2->T, id) != id)
            return false; // id in idset1 not set in idset2
        id = vebsucc (idset1->T, id + 1);
    }

    /* No need to iterate idset2 if counts were equal and all ids in idset1
     * were found in idset2.
     */
    if (count_checked)
        return true;

    id = vebsucc (idset2->T, 0);
    while (id < idset2->T.M) {
        if (vebsucc (idset1->T, id) != id)
            return false; // id in idset2 not set in idset1
        id = vebsucc (idset2->T, id + 1);
    }
    return true;
}

bool idset_has_intersection (const struct idset *a, const struct idset *b)
{
    if (a && b) {
        unsigned int id;

        /*  If there isn't a penalty for idset_count(3), then ensure
         *  we're going to iterate the smaller of the provided idsets
         *  for efficiency:
         */
        if (!(a->flags & IDSET_FLAG_COUNT_LAZY)
            && !(b->flags & IDSET_FLAG_COUNT_LAZY)
            && idset_count (a) < idset_count (b)) {
            const struct idset *tmp = a;
            a = b;
            b = tmp;
        }

        id = idset_first (b);
        while (id != IDSET_INVALID_ID) {
            if (idset_test (a, id))
                return true;
            id = idset_next (b, id);
        }
    }
    return false;
}

int idset_add (struct idset *a, const struct idset *b)
{
    if (!a) {
        errno = EINVAL;
        return -1;
    }
    if (b) {
        unsigned int id;
        id = idset_first (b);
        while (id != IDSET_INVALID_ID) {
            if (idset_set (a, id) < 0)
                return -1;
            id = idset_next (b, id);
        }
    }
    return 0;
}

struct idset *idset_union (const struct idset *a, const struct idset *b)
{
    struct idset *result;

    if (!a) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = idset_copy_flags (a, IDSET_FLAG_AUTOGROW)))
        return NULL;
    if (idset_add (result, b) < 0) {
        idset_destroy (result);
        return NULL;
    }
    return result;
}

int idset_subtract (struct idset *a, const struct idset *b)
{
    if (!a) {
        errno = EINVAL;
        return -1;
    }
    if (b) {
        unsigned int id;

        id = idset_first (b);
        while (id != IDSET_INVALID_ID) {
            if (idset_clear (a, id) < 0)
                return -1;
            id = idset_next (b, id);
        }
    }
    return 0;
}

struct idset *idset_difference (const struct idset *a, const struct idset *b)
{
    struct idset *result;

    if (!a) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = idset_copy (a)))
        return NULL;
    if (idset_subtract (result, b) < 0) {
        idset_destroy (result);
        return NULL;
    }
    return result;
}

struct idset *idset_intersect (const struct idset *a, const struct idset *b)
{
    struct idset *result;
    unsigned int id;

    if (!a || !b) {
        errno = EINVAL;
        return NULL;
    }
    /*  If there isn't a penalty for idset_count(3), then ensure
     *  we start with the smaller of the two idsets for efficiency:
     */
    if (!(a->flags & IDSET_FLAG_COUNT_LAZY)
        && !(b->flags & IDSET_FLAG_COUNT_LAZY)
        && idset_count (b) < idset_count (a)) {
        const struct idset *tmp = a;
        a = b;
        b = tmp;
    }

    if (!(result = idset_copy (a)))
        return NULL;
    id = idset_first (a);
    while (id != IDSET_INVALID_ID) {
        if (!idset_test (b, id) && idset_clear (result, id) < 0) {
            idset_destroy (result);
            return NULL;
        }
        id = idset_next (a, id);
    }
    return result;
}

/* Find the next available id.  If there isn't one, try to grow the set.
 * The grow attempt will fail if IDSET_FLAG_AUTOGROW is not set.
 * Finally call vebdel() to take the id out of the set and return it.
 */
int idset_alloc (struct idset *idset, unsigned int *val)
{
    unsigned int id = IDSET_INVALID_ID;

    if (!idset || !(idset->flags & IDSET_FLAG_INITFULL) || !val) {
        errno = EINVAL;
        return -1;
    }
    if ((idset->flags & IDSET_FLAG_ALLOC_RR)
        && idset->alloc_rr_last != IDSET_INVALID_ID) {
        id = idset_next (idset, idset->alloc_rr_last);
    }
    if (id == IDSET_INVALID_ID)
        id = idset_first (idset);
    if (id == IDSET_INVALID_ID) {
        id = idset_universe_size (idset);
        if (idset_grow (idset, id + 1) < 0)
            return -1;
    }
    // code above ensures that id is a member of idset
    idset_del_nocheck (idset, id);
    if ((idset->flags & IDSET_FLAG_ALLOC_RR))
        idset->alloc_rr_last = id;
    *val = id;
    return 0;
}

/* Return an id to the set, ignoring invalid or out of range ones.
 * This does not catch double-frees.
 */
void idset_free (struct idset *idset, unsigned int val)
{
    if (!idset || !(idset->flags & IDSET_FLAG_INITFULL))
        return;
    idset_put (idset, val);
}

/* Same as above but fail if the id is already in the set.
 */
int idset_free_check (struct idset *idset, unsigned int val)
{
    if (!idset
        || !(idset->flags & IDSET_FLAG_INITFULL)
        || !valid_id (val)
        || val >= idset_universe_size (idset)) {
        errno = EINVAL;
        return -1;
    }
    if (idset_test (idset, val)) {
        errno = EEXIST;
        return -1;
    }
    // code above ensures that id is NOT a member of idset
    idset_put_nocheck (idset, val);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
