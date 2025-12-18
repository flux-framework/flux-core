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
#include "config.h"
#endif

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <jansson.h>
#include <flux/idset.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/ptrint/ptrint.h"
#include "ccan/str/str.h"

#include "rnode.h"

static struct idset *util_idset_add_check (const struct idset *a,
                                           const struct idset *b)
{
    if (idset_count (a) == 0)
        return idset_copy (b);
    if (idset_has_intersection (a, b)) {
        errno = EEXIST;
        return NULL;
    }
    return idset_union (a, b);
}

void rnode_destroy (struct rnode *n)
{
    if (n) {
        int saved_errno = errno;
        free (n->hostname);
        zhashx_destroy (&n->children);
        zhashx_destroy (&n->properties);
        free (n);
        errno = saved_errno;
    }
}

static void rnode_child_destroy (struct rnode_child *c)
{
    if (c) {
        int saved_errno = errno;
        free (c->name);
        idset_destroy (c->avail);
        idset_destroy (c->ids);
        free (c);
        errno = saved_errno;
    }
}

static struct rnode_child *rnode_child_idset (const char *name,
                                              const struct idset *ids,
                                              const struct idset *avail)
{
    struct rnode_child *c = calloc (1, sizeof (*c));
    if (!(c->name = strdup (name)))
        return NULL;
     if (!(c->ids = idset_copy (ids))
        || !(c->avail = idset_copy (avail)))
        goto fail;
    return c;
fail:
    rnode_child_destroy (c);
    return NULL;
}

static struct rnode_child *rnode_child_copy (const struct rnode_child *c)
{
    return rnode_child_idset (c->name, c->ids, c->avail);
}

static int rnode_child_clear (struct rnode_child *c)
{
    /* clearing idsets manually is expensive. It is cheaper to destroy
     * and recraete an empty idset. Update this code when that is no longer
     * the case.
     */
    idset_destroy (c->avail);
    idset_destroy (c->ids);
    if (!(c->avail = idset_create (0, IDSET_FLAG_AUTOGROW))
        || !(c->ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return -1;
    return 0;
}

static void rn_child_free (void **x)
{
    if (x) {
        rnode_child_destroy (*x);
        *x = NULL;
    }
}

/*  Add IDs in idset 'new' to the rnode child 'c'.
 *  It is an error if any of the IDs in 'new' are already set in 'c'.
 */
static int rnode_child_add_idset (struct rnode_child *c,
                                   const struct idset *new)
{
    struct idset *tmp = NULL;
    struct idset *ids = NULL;
    struct idset *avail = NULL;
    int rc = -1;

    if (!(ids = util_idset_add_check (c->ids, new))
        || !(avail = util_idset_add_check (c->avail, new)))
        goto out;

    tmp = c->ids;
    c->ids = ids;
    idset_destroy (tmp);
    ids = NULL;

    tmp = c->avail;
    c->avail = avail;
    idset_destroy (tmp);
    avail = NULL;

    rc = 0;
out:
    idset_destroy (ids);
    idset_destroy (avail);
    return rc;
}

/*  Add 'ids' to resource child 'name' in rnode 'n'. if a child with
 *   'name' does not exist, then add a new child. If 'name' does exist
 *   then add 'ids' to that child (it is an error if one or more ids
 *   are already set in child 'name'.
 */
struct rnode_child * rnode_add_child_idset (struct rnode *n,
                                            const char *name,
                                            const struct idset *ids,
                                            const struct idset *avail)
{
    struct rnode_child *c = zhashx_lookup (n->children, name);

    if (c == NULL) {
        c = rnode_child_idset (name, ids, avail);
        if (!c || zhashx_insert (n->children, name, c) != 0) {
            rnode_child_destroy (c);
            return NULL;
        }
    }
    else {
        if (rnode_child_add_idset (c, ids) < 0)
            c = NULL;
    }
    return c;
}

struct rnode_child * rnode_add_child (struct rnode *n,
                                      const char *name,
                                      const char *ids)
{
    struct rnode_child *c = NULL;
    struct idset *new;
    if (!(new = idset_decode (ids)))
        return NULL;
    c = rnode_add_child_idset (n, name, new, new);
    idset_destroy (new);
    return c;
}

/*  Create a new rnode with no child resources.
 */
struct rnode *rnode_new (const char *name, uint32_t rank)
{
    struct rnode *n = calloc (1, sizeof (*n));
    if (n == NULL)
        return NULL;
    if (name && !(n->hostname = strdup (name)))
        goto fail;
    if (!(n->children = zhashx_new ()))
        goto fail;
    zhashx_set_destructor (n->children, rn_child_free);

    /*  A child named "core" is always required, even if idset
     *   is empty. Add it now:
     */
    if (!(n->cores = rnode_add_child (n, "core", "")))
        goto fail;
    n->rank = rank;
    n->up = true;
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

int rnode_add (struct rnode *orig, struct rnode *n)
{
    int rc = 0;
    struct rnode_child *c;
    if (!orig || !n)
        return -1;

    c = zhashx_first (n->children);
    while (c) {
        if (!rnode_add_child_idset (orig, c->name, c->ids, c->avail))
            return -1;
        c = zhashx_next (n->children);
    }
    if (n->properties) {
        zlistx_t *l = zhashx_keys (n->properties);
        if (l != NULL) {
            const char *property = zlistx_first (l);
            while (property) {
                if (rnode_set_property (orig, property) < 0)
                    rc = -1;
                property = zlistx_next (l);
            }
            zlistx_destroy (&l);
        }
        else
            rc = -1;
    }
    return rc;
}

struct rnode *rnode_create (const char *name, uint32_t rank, const char *ids)
{
    struct rnode *n = rnode_new (name, rank);
    if (n == NULL)
        return NULL;
    if (!(n->cores = rnode_add_child (n, "core", ids)))
        goto fail;
    assert (n->cores == zhashx_lookup (n->children, "core"));
    return (n);
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_create_children (const char *name,
                                     uint32_t rank,
                                     json_t *children)
{
    const char *key = NULL;
    json_t *val = NULL;

    struct rnode *n = rnode_new (name, rank);
    if (n == NULL)
        return NULL;
    json_object_foreach (children, key, val) {
        const char *ids = json_string_value (val);
        struct rnode_child *c = rnode_add_child (n, key, ids);
        if (c == NULL)
            goto fail;
        if (streq (key, "core"))
            n->cores = c;
    }
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_create_idset (const char *name,
                                  uint32_t rank,
                                  struct idset *ids)
{
    struct rnode *n = rnode_new (name, rank);
    if (n == NULL)
        return NULL;
    if (!(n->cores = rnode_add_child_idset (n, "core", ids, ids)))
        goto fail;
    return (n);
fail:
    rnode_destroy (n);
    return NULL;
}

int rnode_set_property (struct rnode *n, const char *name)
{
    if (!n->properties && !(n->properties = zhashx_new ())) {
        errno = ENOMEM;
        return -1;
    }
    /*
     *  zhashx_insert () supposedly returns -1 when 'name' already
     *   exists in the hash, but setting an existing property is
     *   not an error. Therefore, ignore this error.
     */
    (void) zhashx_insert (n->properties, name, int2ptr (1));
    return 0;
}

void rnode_remove_property (struct rnode *n, const char *name)
{
    if (n->properties)
        zhashx_delete (n->properties, name);
}

bool rnode_has_property (struct rnode *n, const char *name)
{
    return (n->properties && zhashx_lookup (n->properties, name));
}

static int rnode_set_empty (struct rnode *n)
{
    int count = 0;
    struct rnode_child *c = zhashx_first (n->children);
    while (c) {
        idset_destroy (c->avail);
        if (!(c->avail = idset_copy (c->ids)))
            return -1;
        count += idset_count (c->ids);
        c = zhashx_next (n->children);
    }
    return count;
}

static int rnode_set_alloc (struct rnode *n)
{
    int count = 0;
    struct rnode_child *c;

    if (!n)
        return -1;

    c = zhashx_first (n->children);
    while (c) {
        if (idset_subtract (c->ids, c->avail) < 0)
            return -1;
        idset_destroy (c->avail);
        if (!(c->avail = idset_copy (c->ids)))
            return -1;
        count += idset_count (c->ids);
        c = zhashx_next (n->children);
    }
    return count;
}

static int rnode_set_avail (struct rnode *n)
{
    int count = 0;
    struct rnode_child *c;

    if (!n)
        return -1;

    c = zhashx_first (n->children);
    while (c) {
        idset_destroy (c->ids);
        if (!(c->ids = idset_copy (c->avail)))
            return -1;
        count += idset_count (c->ids);
        c = zhashx_next (n->children);
    }
    return count;
}

static zhashx_t *rnode_children_copy (const struct rnode *n)
{
    zhashx_t *copy;

    /*  Temporarily set duplicator on orig.
     *  (if duplicator permanently set then zhashx_insert() also
     *   duplicates items, which we don't want
     */
    zhashx_set_duplicator (n->children,
                           (zhashx_duplicator_fn *) rnode_child_copy);
    copy = zhashx_dup (n->children);
    zhashx_set_destructor (copy, rn_child_free);
    zhashx_set_duplicator (copy, NULL);
    zhashx_set_duplicator (n->children, NULL);
    return copy;
}

struct rnode *rnode_copy (const struct rnode *orig)
{
    struct rnode *n = rnode_new (orig->hostname, orig->rank);
    if (!n)
        return NULL;
    zhashx_destroy (&n->children);
    if (!(n->children = rnode_children_copy (orig)))
        goto fail;
    if (!(n->cores = zhashx_lookup (n->children, "core")))
        goto fail;
    if (orig->properties
        && !(n->properties = zhashx_dup (orig->properties)))
        goto fail;
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_copy_cores (const struct rnode *orig)
{
    struct rnode *n = rnode_copy (orig);
    if (n) {
        const char *name;
        zlistx_t *keys = zhashx_keys (n->children);
        if (!keys)
            goto error;
        name = zlistx_first (keys);
        while (name) {
            if (!streq (name, "core"))
                zhashx_delete (n->children, name);
            name = zlistx_next (keys);
        }
        zlistx_destroy (&keys);
        return n;
    }
error:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_copy_empty (const struct rnode *orig)
{
    struct rnode *n = rnode_copy (orig);
    if (!n || rnode_set_empty (n) <= 0)
        goto fail;
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_copy_avail (const struct rnode *orig)
{
    struct rnode *n = rnode_copy (orig);
    if (!n || rnode_set_avail (n) <= 0)
        goto fail;
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

struct rnode *rnode_copy_alloc (const struct rnode *orig)
{
    struct rnode *n = rnode_copy (orig);
    if (!n || rnode_set_alloc (n) <= 0)
        goto fail;
    return n;
fail:
    rnode_destroy (n);
    return NULL;
}

bool rnode_empty (const struct rnode *n)
{
    int count = 0;
    struct rnode_child *c;

    c = zhashx_first (n->children);
    while (c) {
        count += idset_count (c->ids);
        c = zhashx_next (n->children);
    }
    return count == 0;
}

static bool rnode_child_ignore (struct rnode_child *nc, int ignore_mask)
{
    if (((ignore_mask & RNODE_IGNORE_CORE) && streq (nc->name, "core"))
        || ((ignore_mask & RNODE_IGNORE_GPU) && streq (nc->name, "gpu")))
        return true;
    return false;
}

struct rnode *rnode_diff (const struct rnode *a, const struct rnode *b)
{
    return rnode_diff_ex (a, b, 0);
}

struct rnode *rnode_diff_ex (const struct rnode *a,
                             const struct rnode *b,
                             int ignore_mask)
{
    struct rnode_child *c;
    struct rnode *n = rnode_copy (a);
    if (!n)
        return NULL;
    c = zhashx_first (b->children);
    while (c) {
        struct rnode_child *nc = zhashx_lookup (n->children, c->name);
        if (nc) {
            if (idset_equal (nc->ids, c->ids)) {
                /* Optimization: if nc->ids == c->ids, then just replace
                 * nc with empty idsets. This will be much faster than
                 * idset_subtract().
                 */
                if (rnode_child_clear (nc) < 0)
                    goto err;
            }
            else if (idset_subtract (nc->ids, c->ids) < 0
                     || idset_subtract (nc->avail, c->avail) < 0)
                goto err;

            /*  For non-core resources, remove empty sets:
             */
            if (!streq (nc->name, "core")
                && idset_count (nc->ids) == 0)
                zhashx_delete (n->children, nc->name);
        }
        c = zhashx_next (b->children);
    }
    /* Clear ignored resources
     */
    if (ignore_mask) {
        c = zhashx_first (n->children);
        while (c) {
            if (rnode_child_ignore (c, ignore_mask)
                && rnode_child_clear (c) < 0)
                goto err;
            c = zhashx_next (n->children);
        }
    }
    return n;
err:
    rnode_destroy (n);
    return NULL;
}

int rnode_alloc (struct rnode *n, int count, struct idset **setp)
{
    struct idset *ids = NULL;
    unsigned int i;
    if (!n->up) {
        errno = EHOSTDOWN;
        return -1;
    }
    if (idset_count (n->cores->avail) < count) {
        errno = ENOSPC;
        return -1;
    }
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return -1;
    i = idset_first (n->cores->avail);
    while (count--) {
        idset_set (ids, i);
        idset_clear (n->cores->avail, i);
        i = idset_next (n->cores->avail, i);
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
        if (!idset_test (n->cores->ids, i)) {
            errno = ENOENT;
            return false;
        }
        if (!idset_test (n->cores->avail, i)) {
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
        idset_clear (n->cores->avail, i);
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
        if (!idset_test (n->cores->ids, i)) {
            errno = ENOENT;
            return false;
        }
        if (idset_test (n->cores->avail, i)) {
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
        idset_set (n->cores->avail, i);
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

int rnode_avail_total (const struct rnode *n)
{
    int count = 0;
    struct rnode_child *c;

    if (!n->up)
        return 0;

    c = zhashx_first (n->children);
    while (c) {
        count += idset_count (c->avail);
        c = zhashx_next (n->children);
    }
    return count;
}

size_t rnode_avail (const struct rnode *n)
{
    if (n->up)
        return (idset_count (n->cores->avail));
    return 0;
}

size_t rnode_count (const struct rnode *n)
{
    return (idset_count (n->cores->ids));
}

size_t rnode_count_type (const struct rnode *n, const char *type)
{
    struct rnode_child *c = zhashx_lookup (n->children, type);
    if (c)
        return (idset_count (c->ids));
    return 0;
}

/*  Compare two values from idset_first()/idset_next():
 *  Returns:
 *    0   : if x == y
 *    > 0 : if x > y
 *    < 0 : if x < y
 *
 *  Where IDSET_INVALID_ID is considered to come before all numbers.
 */
static int idset_val_cmp (unsigned int x, unsigned int y)
{
    if (x == y)
        return 0;
    else if (x == IDSET_INVALID_ID)
        return -1;
    else if (y == IDSET_INVALID_ID)
        return  1;
    else
        return (x - y);
}

static int idset_cmp (struct idset *set1, struct idset *set2)
{
    int rv = 0;
    if (!idset_equal (set1, set2)) {
        /*  Sort on the first non-equal integer (see idset_val_cmp())
         */
        unsigned int a = idset_first (set1);
        unsigned int b = idset_first (set2);
        while ((rv = idset_val_cmp (a, b)) == 0) {
            a = idset_next (set1, a);
            b = idset_next (set2, b);
        }
    }
    return rv;
}

int rnode_cmp (const struct rnode *a, const struct rnode *b)
{
    int rv = 0;
    /*  All avail idsets must match */
    struct rnode_child *ca;
    struct rnode_child *cb;

    /* If two nodes do not have the same number of children types,
     *  then they are not identical. Order doesn't matter here...
     */
    if (zhashx_size (a->children) != zhashx_size (b->children))
        return -1;
    ca = zhashx_first (a->children);
    while (ca) {
        if (!(cb = zhashx_lookup (b->children, ca->name)))
            return -1;
        if ((rv = idset_cmp (ca->avail, cb->avail)) != 0)
            break;
        ca = zhashx_next (a->children);
    }
    return rv;
}

struct rnode_child * rnode_child_intersect (const struct rnode_child *a,
                                            const struct rnode_child *b)
{
    struct rnode_child *c = NULL;
    struct idset *ids = idset_intersect (a->ids, b->ids);
    struct idset *avail = idset_intersect (a->avail, b->avail);
    if (!ids || !avail)
        goto out;
    if (!idset_count (ids) && !idset_count (avail))
        goto out;
    c = rnode_child_idset (a->name, ids, avail);
out:
    idset_destroy (ids);
    idset_destroy (avail);
    return c;
}

int rnode_hostname_cmp (const struct rnode *a,
                        const struct rnode *b)
{
    /*  N.B.: missing hostname is ignored for now for backwards
     *   compatibility.
     */
    if (!a->hostname || !b->hostname)
        return 0;
    return (strcmp (a->hostname, b->hostname));
}

struct rnode *rnode_intersect (const struct rnode *a,
                               const struct rnode *b)
{
    struct rnode *result = NULL;
    struct rnode_child *cb = NULL;
    struct rnode_child *ca = NULL;

    if (!a || !b)
        return NULL;

    if (a->rank != b->rank
        || rnode_hostname_cmp (a, b) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = rnode_new (a->hostname, a->rank)))
        return NULL;

    ca = zhashx_first (a->children);
    while (ca) {
        if ((cb = zhashx_lookup (b->children, ca->name))) {
            struct rnode_child *c = rnode_child_intersect (ca, cb);
            if (c &&
                rnode_add_child_idset (result, c->name, c->ids, c->avail) < 0)
                goto err;
            rnode_child_destroy (c);
        }
        ca = zhashx_next (a->children);
    }
    return result;
err:
    rnode_destroy (result);
    return NULL;
}

static int rnode_child_remap (struct rnode_child *c)
{
    unsigned int i;
    unsigned int n;
    size_t count = idset_count (c->ids);

    /*  No need to remap if no IDs are set for this child, or
     *   c->ids is already [0, count-1]
     */
    if (count == 0 
        || (idset_first (c->ids) == 0
            && idset_last (c->ids) == count - 1))
        return 0;
    
    /*  First, remap c->avail using c->ids as a reference
     */
    n = 0;
    i = idset_first (c->ids);
    while (i != IDSET_INVALID_ID) {
        if (idset_test (c->avail, i)) {
            idset_clear (c->avail, i);
            idset_set (c->avail, n);
        }
        i = idset_next (c->ids, i);
        n++;
    }

    /*  Now, remap c->ids by simply setting ids 0 - count-1
     */
    if (idset_range_clear (c->ids, 0, idset_last (c->ids)) < 0)
        return -1;
    if (idset_range_set (c->ids, 0, count - 1) < 0)
        return -1;
    return 0;
}

int rnode_remap (struct rnode *n, zhashx_t *noremap)
{
    struct rnode_child *c;

    c = zhashx_first (n->children);
    while (c) {
        if (!noremap || !zhashx_lookup (noremap, c->name))
            if (rnode_child_remap (c) < 0)
                return -1;
        c = zhashx_next (n->children);
    }
    return 0;
}

static json_t *children_encode (const struct rnode *n)
{
    struct rnode_child *c;
    json_t *o = json_object ();

    c = zhashx_first (n->children);
    while (c) {
        if (idset_count (c->avail) > 0) {
            char *ids = idset_encode (c->avail, IDSET_FLAG_RANGE);
            json_t *val;
            if (!ids)
                goto fail;
            if (!(val = json_string (ids))) {
                free (ids);
                goto fail;
            }
            free (ids);
            if (json_object_set_new (o, c->name, val) < 0)
                goto fail;
        }
        c = zhashx_next (n->children);
    }
    return o;
fail:
    json_decref (o);
    return NULL;
}

json_t *rnode_encode (const struct rnode *n, const struct idset *ids)
{
    char *ranks = NULL;
    json_t *children = NULL;
    json_t *o = NULL;

    if (!(ranks = idset_encode (ids, IDSET_FLAG_RANGE)))
        return NULL;
    if (!(children = children_encode (n)))
        goto done;
    if (!(o = json_pack ("{s:s s:o}",
                         "rank", ranks,
                         "children", children)))
        goto done;
    children = NULL;
done:
    json_decref (children);
    free (ranks);
    return o;
}

/* vi: ts=4 sw=4 expandtab
 */
