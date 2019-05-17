/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#    include "config.h"
#endif

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <flux/idset.h>

#include "rnode.h"

void rnode_destroy (struct rnode *n)
{
    if (n) {
        idset_destroy (n->avail);
        idset_destroy (n->ids);
        free (n);
    }
}

struct rnode *rnode_create (uint32_t rank, const char *ids)
{
    struct rnode *n = calloc (1, sizeof (*n));
    if (n == NULL)
        return NULL;
    n->rank = rank;
    if (!(n->ids = idset_decode (ids)) || !(n->avail = idset_copy (n->ids)))
        goto fail;
    return (n);
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_create_idset (uint32_t rank, struct idset *ids)
{
    struct rnode *n = calloc (1, sizeof (*n));
    if (n == NULL)
        return NULL;
    n->rank = rank;
    if (!(n->ids = idset_copy (ids)) || !(n->avail = idset_copy (ids)))
        goto fail;
    return (n);
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_create_count (uint32_t rank, int count)
{
    struct rnode *n = calloc (1, sizeof (*n));
    if (n == NULL)
        return NULL;
    n->rank = rank;
    if (!(n->ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || (idset_range_set (n->ids, 0, count - 1) < 0)
        || !(n->avail = idset_copy (n->ids)))
        goto fail;
    return (n);
fail:
    rnode_destroy (n);
    return NULL;
}

int rnode_alloc (struct rnode *n, int count, struct idset **setp)
{
    struct idset *ids = NULL;
    unsigned int i;
    if (idset_count (n->avail) < count) {
        errno = ENOSPC;
        return -1;
    }
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return -1;
    i = idset_first (n->avail);
    while (count--) {
        idset_set (ids, i);
        idset_clear (n->avail, i);
        i = idset_next (n->avail, i);
    }
    if (setp != NULL)
        *setp = ids;
    return (0);
}

/*
 *  Test if idset `ids` is a valid set of ids to allocate from the rnode `n`
 *  Return true if the idset is valid, false otherwise.
 */
static bool alloc_ids_valid (struct rnode *n, struct idset *ids)
{
    unsigned int i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (!idset_test (n->ids, i)) {
            errno = ENOENT;
            return false;
        }
        if (!idset_test (n->avail, i)) {
            errno = EEXIST;
            return false;
        }
        i = idset_next (ids, i);
    }
    return (true);
}

int rnode_alloc_idset (struct rnode *n, struct idset *ids)
{
    unsigned int i;
    if (!ids) {
        errno = EINVAL;
        return -1;
    }
    if (!alloc_ids_valid (n, ids))
        return -1;
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        idset_clear (n->avail, i);
        i = idset_next (ids, i);
    }
    return 0;
}

/*
 *  Test if idset `ids` is a valid set of ids to free from the rnode `n`
 *  Return true if the idset is valid, false otherwise.
 */
static bool free_ids_valid (struct rnode *n, struct idset *ids)
{
    unsigned int i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (!idset_test (n->ids, i)) {
            errno = ENOENT;
            return false;
        }
        if (idset_test (n->avail, i)) {
            errno = EEXIST;
            return false;
        }
        i = idset_next (ids, i);
    }
    return (true);
}

int rnode_free_idset (struct rnode *n, struct idset *ids)
{
    unsigned int i;
    if (!ids) {
        errno = EINVAL;
        return -1;
    }
    if (!free_ids_valid (n, ids))
        return -1;
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        idset_set (n->avail, i);
        i = idset_next (ids, i);
    }
    return 0;
}

int rnode_free (struct rnode *n, const char *s)
{
    int saved_errno;
    struct idset *ids = idset_decode (s);
    int rc = rnode_free_idset (n, ids);
    saved_errno = errno;
    idset_destroy (ids);
    errno = saved_errno;
    return (rc);
}

size_t rnode_avail (const struct rnode *n)
{
    return (idset_count (n->avail));
}

size_t rnode_count (const struct rnode *n)
{
    return (idset_count (n->ids));
}

/* vi: ts=4 sw=4 expandtab
 */
