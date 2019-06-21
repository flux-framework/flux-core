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

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <czmq.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "rnode.h"
#include "rlist.h"
#include "libjj.h"

void rlist_destroy (struct rlist *rl)
{
    if (rl) {
        zlistx_destroy (&rl->nodes);
        free (rl);
    }
}

static void rn_free_fn (void **x)
{
    rnode_destroy (*(struct rnode **)x);
    *x = NULL;
}

struct rlist *rlist_create (void)
{
    struct rlist *rl = calloc (1, sizeof (*rl));
    if (!(rl->nodes = zlistx_new ()))
        goto err;
    zlistx_set_destructor (rl->nodes, rn_free_fn);
    return (rl);
err:
    rlist_destroy (rl);
    return (NULL);
}

struct rlist *rlist_copy_empty (const struct rlist *orig)
{
    struct rnode *n;
    struct rlist *rl = rlist_create ();
    if (!rl)
        return NULL;
    n = zlistx_first (orig->nodes);
    while (n) {
        n = rnode_create_idset (n->rank, n->ids);
        if (!n || !zlistx_add_end (rl->nodes, n))
            goto fail;
        rl->total += rnode_count (n);
        n = zlistx_next (orig->nodes);
    }
    rl->avail = rl->total;
    return rl;
fail:
    rlist_destroy (rl);
    return NULL;
}


static struct rnode *rlist_find_rank (struct rlist *rl, uint32_t rank)
{
    struct rnode *n = zlistx_first (rl->nodes);
    while (n) {
        if (n->rank == rank)
            return (n);
        n = zlistx_next (rl->nodes);
    }
    return NULL;
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

static int idset_add_set (struct idset *set, struct idset *new)
{
    unsigned int i = idset_first (new);
    while (i != IDSET_INVALID_ID) {
        if (idset_test (set, i)) {
            errno = EEXIST;
            return -1;
        }
        if (idset_set (set, i) < 0)
            return -1;
        i = idset_next (new, i);
    }
    return 0;
}

static int idset_remove_set (struct idset *set, struct idset *remove)
{
    unsigned int i = idset_first (remove);
    while (i != IDSET_INVALID_ID) {
        if (!idset_test (set, i)) {
            errno = ENOENT;
            return -1;
        }
        if (idset_clear (set, i) < 0)
            return -1;
        i = idset_next (remove, i);
    }
    return 0;
}

static int rlist_add_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *found = rlist_find_rank (rl, n->rank);
    if (found) {
        if (idset_add_set (found->ids, n->ids) < 0)
            return (-1);
        if (idset_add_set (found->avail, n->avail) < 0) {
            idset_remove_set (found->ids, n->ids);
            return (-1);
        }
    }
    else if (!zlistx_add_end (rl->nodes, n))
        return -1;
    rl->total += rnode_count (n);
    rl->avail += rnode_avail (n);
    if (found)
        rnode_destroy (n);
    return 0;
}

static int rlist_append (struct rlist *rl, const char *ranks, json_t *e)
{
    int rc = -1;
    unsigned int n;
    unsigned int i;
    const char *corelist = NULL;
    struct rnode *node;
    struct idset *ids = idset_decode (ranks);
    json_error_t err;

    if (!ids || json_unpack_ex (e, &err, 0, "{s:i,s?s}",
                                "Core", &n,
                                "cpuset", &corelist) < 0)
        goto out;
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (corelist)
            node = rnode_create (i, corelist);
        else
            node = rnode_create_count (i, n);
        if (!node || rlist_add_rnode (rl, node) < 0) {
            rnode_destroy (node);
            goto out;
        }
        i = idset_next (ids, i);
    }
    rc = 0;
out:
    idset_destroy (ids);
    return rc;
}

struct rlist *rlist_from_hwloc_by_rank (const char *by_rank)
{
    struct rlist *rl = NULL;
    const char *key = NULL;
    json_t *entry = NULL;

    json_t *o = json_loads (by_rank, 0, NULL);
    if (o == NULL)
        return NULL;
    if (!(rl = rlist_create ()))
        goto err;

    json_object_foreach (o, key, entry) {
        if (rlist_append (rl, key, entry) < 0)
            goto err;
    }
    json_decref (o);

    return (rl);
err:
    json_decref (o);
    rlist_destroy (rl);
    return NULL;
}

int rlist_append_rank (struct rlist *rl, unsigned int rank, const char *ids)
{
    struct rnode *n = rnode_create (rank, ids);
    if (!n || rlist_add_rnode (rl, n) < 0) {
        rnode_destroy (n);
        return -1;
    }
    return 0;
}

int rlist_append_ranks (struct rlist *rl, const char *rank, const char *ids)
{
    int rc = -1;
    unsigned int i;
    struct idset * ranks = idset_decode (rank);
    if (!ranks)
        return -1;
    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if (rlist_append_rank (rl, i, ids) < 0)
            goto err;
        i = idset_next (ranks, i);
    }
    rc = 0;
err:
    idset_destroy (ranks);
    return rc;
}

int rlist_append_idset (struct rlist *rl, int rank, struct idset *idset)
{
    struct rnode *n = rnode_create_idset (rank, idset);
    if (!n || rlist_add_rnode (rl, n) < 0) {
        rnode_destroy (n);
        return -1;
    }
    return 0;
}

static int rlist_append_rank_entry (struct rlist *rl, json_t *entry,
                                    json_error_t *ep)
{
    const char *ranks;
    const char *cores;
    if (json_unpack_ex (entry, ep, 0,
                        "{s:s,s:{s:s}}",
                        "rank", &ranks,
                        "children", "core", &cores) < 0) {
        return -1;
    }
    return rlist_append_ranks (rl, ranks, cores);
}

struct rlist *rlist_from_R (const char *s)
{
    int i, version;
    struct rlist *rl = NULL;
    json_t *entry = NULL;
    json_t *R_lite = NULL;
    json_error_t error;

    json_t *o = json_loads (s, 0, NULL);
    if (o == NULL)
        return NULL;
    if (!o || json_unpack_ex (o, &error, 0,
                              "{s:i,s:{s:o}}",
                              "version", &version,
                              "execution", "R_lite", &R_lite) < 0) {
        goto err;
    }
    if (version != 1)
        goto err;
    if (!(rl = rlist_create ()))
        goto err;
    json_array_foreach (R_lite, i, entry) {
        if (rlist_append_rank_entry (rl, entry, &error) < 0)
            goto err;
    }
    json_decref (o);
    return (rl);
err:
    rlist_destroy (rl);
    json_decref (o);
    return (NULL);
}

/* Helper for rlist_compressed */
struct multi_rnode {
    struct idset *ids;
    const struct rnode *rnode;
};

static int multi_rnode_cmp (struct multi_rnode *x, struct idset *ids)
{
    return (idset_cmp (x->rnode->avail, ids));
}

static void multi_rnode_destroy (struct multi_rnode **mrn)
{
    if (mrn && *mrn) {
        (*mrn)->rnode = NULL;
        idset_destroy ((*mrn)->ids);
        free (*mrn);
        *mrn = NULL;
    }
}

struct multi_rnode * multi_rnode_create (struct rnode *rnode)
{
    struct multi_rnode *mrn = calloc (1, sizeof (*mrn));
    if (mrn == NULL)
        return NULL;
    if (!(mrn->ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || (idset_set (mrn->ids, rnode->rank) < 0))
        goto fail;
    mrn->rnode = rnode;
    return (mrn);
fail:
    multi_rnode_destroy (&mrn);
    return NULL;
}

json_t *multi_rnode_tojson (struct multi_rnode *mrn)
{
    json_t *o = NULL;
    char *ids = idset_encode (mrn->rnode->avail, IDSET_FLAG_RANGE);
    char *ranks = idset_encode (mrn->ids, IDSET_FLAG_RANGE);

    if (!ids || !ranks)
        goto done;
    o = json_pack ("{s:s,s:{s:s}}", "rank", ranks, "children", "core", ids);
done:
    free (ids);
    free (ranks);
    return (o);
}

static zlistx_t * rlist_mrlist (struct rlist *rl)
{
    struct rnode *n = NULL;
    struct multi_rnode *mrn = NULL;
    zlistx_t *l = zlistx_new ();

    zlistx_set_comparator (l, (czmq_comparator *) multi_rnode_cmp);
    zlistx_set_destructor (l, (czmq_destructor *) multi_rnode_destroy);

    n = zlistx_first (rl->nodes);
    while (n) {
        if (zlistx_find (l, n->avail)) {
            if (!(mrn = zlistx_handle_item (zlistx_cursor (l)))
              || idset_set (mrn->ids, n->rank) < 0) {
                goto fail;
            }
        }
        else if (rnode_avail (n) > 0) {
            if (!(mrn = multi_rnode_create (n))
                || !zlistx_add_end (l, mrn)) {
                goto fail;
            }
        }
        n = zlistx_next (rl->nodes);
    }
    return (l);
fail:
    zlistx_destroy (&l);
    return NULL;
}

static json_t * rlist_compressed (struct rlist *rl)
{
    struct multi_rnode *mrn = NULL;
    json_t *o = json_array ();
    zlistx_t *l = rlist_mrlist (rl);

    if (!l)
        return NULL;
    mrn = zlistx_first (l);
    while (mrn) {
        json_t *entry = multi_rnode_tojson (mrn);
        if (!entry || json_array_append_new (o, entry) != 0) {
            json_decref (entry);
            goto fail;
        }
        mrn = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return (o);
fail:
    zlistx_destroy (&l);
    json_decref (o);
    return NULL;
}

static int
sprintfcat (char **s, size_t *sz, size_t *lenp, const char *fmt, ...)
{
    int done = false;
    va_list ap;
    int n = 0;
    while (!done) {
        int nleft = *sz-*lenp;
        va_start (ap, fmt);
        n = vsnprintf ((*s)+*lenp, nleft, fmt, ap);
        if (n < 0 || n >= nleft) {
            char *p;
            *sz += 128;
            if (!(p = realloc (*s, *sz)))
                return -1;
            *s = p;
        }
        else
            done = true;
        va_end (ap);
    }
    *lenp += n;
    return (n);
}

char * rlist_dumps (struct rlist *rl)
{
    int flags = IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS;
    char * result = NULL;
    size_t len = 0;
    size_t size = 64;
    struct multi_rnode *mrn = NULL;
    zlistx_t *l = NULL;

    if (rl == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (!(l = rlist_mrlist (rl))
        || !(result = calloc (size, sizeof (char))))
        goto fail;

    mrn = zlistx_first (l);
    while (mrn) {
        char *ranks = idset_encode (mrn->ids, flags);
        char *cores = idset_encode (mrn->rnode->avail, flags);
        if (sprintfcat (&result, &size, &len , "%srank%s/core%s",
                         result[0] != '\0' ? " ": "",
                         ranks, cores) < 0)
            goto fail;
        free (ranks);
        free (cores);
        mrn = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return (result);
fail:
    free (result);
    zlistx_destroy (&l);
    return NULL;
}

json_t *rlist_to_R (struct rlist *rl)
{
    json_t *R = NULL;
    json_t *R_lite = rlist_compressed (rl);
    if (!R_lite)
        return NULL;
    if (!(R = json_pack ("{s:i, s:{s:o}}",
                         "version", 1,
                         "execution",
                         "R_lite", R_lite)))
        json_decref (R_lite);
    return (R);
}

static int by_rank (const void *item1, const void *item2)
{
    const struct rnode *x = item1;
    const struct rnode *y = item2;
    return (x->rank - y->rank);
}

static int by_avail (const void *item1, const void *item2)
{
    int n;
    const struct rnode *x = item1;
    const struct rnode *y = item2;
    if ((n = rnode_avail (x) - rnode_avail (y)) == 0)
        n = by_rank (x, y);
    return n;
}

static int by_used (const void *item1, const void *item2)
{
    int n;
    const struct rnode *x = item1;
    const struct rnode *y = item2;
    if ((n = rnode_avail (y) - rnode_avail (x)) == 0)
        n = by_rank (x, y);
    return n;
}

static int rlist_rnode_alloc (struct rlist *rl, struct rnode *n,
                              int count, struct idset **idsetp)
{
    if (!n || rnode_alloc (n, count, idsetp) < 0)
        return -1;
    rl->avail -= idset_count (*idsetp);
    return 0;
}

#if 0
static uint32_t rlist_rnode_rank (struct rlist *rl)
{
    struct rnode *n = zlistx_item (rl->nodes);
    if (n)
        return n->rank;
    else
        return (uint32_t)-1;
}
#endif

static struct rnode *rlist_first (struct rlist *rl)
{
    return zlistx_first (rl->nodes);
}

static struct rnode *rlist_next (struct rlist *rl)
{
    return zlistx_next (rl->nodes);
}

/*
 *  Allocate the first available N slots of size cores_per_slot from
 *   resource list rl after sorting the nodes with the current sort strategy.
 */
static struct rlist * rlist_alloc_first_fit (struct rlist *rl,
                                             int cores_per_slot,
                                             int slots)
{
    int rc;
    struct idset *ids = NULL;
    struct rnode *n = NULL;
    struct rlist *result = NULL;

    zlistx_sort (rl->nodes);

    if (!(n = rlist_first (rl)))
        return NULL;

    if (!(result = rlist_create ()))
        return NULL;

    /* 2. assign slots to first nodes where they fit
     */
    while (n && slots) {
        /*  Try to allocate a slot on this node. If we fail with ENOSPC,
         *   then advance to the next node and try again.
         */
        if ((rc = rlist_rnode_alloc (rl, n, cores_per_slot, &ids)) < 0) {
            if (errno != ENOSPC)
                goto unwind;
            n = rlist_next (rl);
            continue;
        }
        /*  Append the allocated cores to the result set and continue
         *   if needed
         */
        rc = rlist_append_idset (result, n->rank, ids);
        idset_destroy (ids);
        if (rc < 0)
            goto unwind;
        slots--;
    }
    if (slots != 0) {
unwind:
        rlist_free (rl, result);
        rlist_destroy (result);
        errno = ENOSPC;
        return NULL;
    }
    return result;
}

/*
 *  Allocate `slots` of size cores_per_slot from rlist `rl` and return
 *   the result. Sorts the node list by smallest available first, so that
 *   we get something like "best fit". (minimize nodes used)
 */
static struct rlist * rlist_alloc_best_fit (struct rlist *rl,
                                            int cores_per_slot,
                                            int slots)
{
    zlistx_set_comparator (rl->nodes, by_avail);
    return rlist_alloc_first_fit (rl, cores_per_slot, slots);
}

/*
 *  Allocate `slots` of size cores_per_slot from rlist `rl` and return
 *   the result. Sorts the node list by least utilized first, so that
 *   we get something like "worst fit". (Spread jobs across nodes)
 */
static struct rlist * rlist_alloc_worst_fit (struct rlist *rl,
                                             int cores_per_slot,
                                             int slots)
{
    zlistx_set_comparator (rl->nodes, by_used);
    return rlist_alloc_first_fit (rl, cores_per_slot, slots);
}


static zlistx_t *rlist_get_nnodes (struct rlist *rl, int nnodes)
{
    struct rnode *n;
    zlistx_t *l = zlistx_new ();
    if (!l)
        return NULL;
    n = zlistx_first (rl->nodes);
    while (nnodes > 0) {
        if (zlistx_add_end (l, n) < 0)
            goto err;
        n = zlistx_next (rl->nodes);
        nnodes--;
    }
    return (l);
err:
    zlistx_destroy (&l);
    return NULL;
}

/*  Allocate 'slots' of size 'cores_per_slot' across exactly `nnodes`.
 *  Works by getting the first N least utilized nodes and spreading
 *  the nslots evenly across the result.
 */
static struct rlist *rlist_alloc_nnodes (struct rlist *rl, int nnodes,
                                         int cores_per_slot, int slots)
{
    struct rlist *result = NULL;
    struct rnode *n = NULL;
    zlistx_t *cl = NULL;

    if (rlist_nnodes (rl) < nnodes) {
        errno = ENOSPC;
        return NULL;
    }
    if (slots < nnodes) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = rlist_create ()))
        return NULL;

    /* 1. sort rank list by used cores ascending:
     */
    zlistx_set_comparator (rl->nodes, by_used);
    zlistx_sort (rl->nodes);

    /* 2. get a list of the first n nodes
     */
    if (!(cl = rlist_get_nnodes (rl, nnodes)))
        return NULL;

    /* We will sort candidate list by used cores on each iteration to
     *  ensure even spread of slots across nodes
     */
    zlistx_set_comparator (cl, by_used);

    /*
     * 3. divide slots across all nodes, placing each slot
     *    on most empty node first
     */
    while (slots > 0) {
        int rc;
        struct idset *ids = NULL;
        n = zlistx_first (cl);
        /*
         * if we can't allocate on this node, give up. Since it is the
         *  least loaded node from the least loaded nodelist, we know
         *  we don't have enough resources to satisfy request.
         */
        if (rlist_rnode_alloc (rl, n, cores_per_slot, &ids) < 0)
            goto unwind;
        rc = rlist_append_idset (result, n->rank, ids);
        idset_destroy (ids);
        if (rc < 0)
            goto unwind;

        /*  Reorder the current item in list so we get least loaded node
         *   at front again for the next call to zlistx_first()
         */
        zlistx_reorder (cl, zlistx_cursor (cl), false);
        slots--;
    }
    zlistx_destroy (&cl);
    return result;
unwind:
    zlistx_destroy (&cl);
    rlist_free (rl, result);
    rlist_destroy (result);
    errno = ENOSPC;
    return NULL;
}

static struct rlist *rlist_try_alloc (struct rlist *rl, const char *mode,
                                      int nnodes, int slots, int cores_per_slot)
{
    struct rlist *result = NULL;

    if (!rl) {
        errno = EINVAL;
        return NULL;
    }

    /*  Reset default sort to order nodes by "rank" */
    zlistx_set_comparator (rl->nodes, by_rank);

    if (nnodes > 0)
        result = rlist_alloc_nnodes (rl, nnodes, cores_per_slot, slots);
    else if (mode == NULL || strcmp (mode, "worst-fit") == 0)
        result = rlist_alloc_worst_fit (rl, cores_per_slot, slots);
    else if (mode && strcmp (mode, "best-fit") == 0)
        result = rlist_alloc_best_fit (rl, cores_per_slot, slots);
    else if (mode && strcmp (mode, "first-fit") == 0)
        result = rlist_alloc_first_fit (rl, cores_per_slot, slots);
    else
        errno = EINVAL;
    return result;
}

/*  Determine if allocation request is feasible for rlist `rl`.
 */
static bool rlist_alloc_feasible (const struct rlist *rl, const char *mode,
                                  int nnodes, int slots, int slotsz)
{
    bool rc = false;
    struct rlist *result = NULL;
    struct rlist *all = rlist_copy_empty (rl);
    if (all && (result = rlist_try_alloc (all, mode, nnodes, slots, slotsz)))
        rc = true;
    rlist_destroy (all);
    rlist_destroy (result);
    return rc;
}

struct rlist *rlist_alloc (struct rlist *rl, const char *mode,
                          int nnodes, int slots, int slotsz)
{
    int total = slots * slotsz;
    struct rlist *result = NULL;

    if (slots <= 0 || slotsz <= 0 || nnodes < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (total > rl->total) {
        errno = EOVERFLOW;
        return NULL;
    }
    if (total > rl->avail) {
        if (rlist_alloc_feasible (rl, mode, nnodes, slots, slotsz))
            errno = ENOSPC;
        else
            errno = EOVERFLOW;
        return NULL;
    }

    /*
     *   Try allocation. If it fails with not enough resources (ENOSPC),
     *    then try again on an empty copy of rlist to see the request could
     *    *ever* be satisfied. Adjust errno to EOVERFLOW if not.
     */
    result = rlist_try_alloc (rl, mode, nnodes, slots, slotsz);
    if (!result && (errno == ENOSPC)) {
        if (rlist_alloc_feasible (rl, mode, nnodes, slots, slotsz))
            errno = ENOSPC;
        else
            errno = EOVERFLOW;
    }
    return (result);
}

static int rlist_free_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *rnode = rlist_find_rank (rl, n->rank);
    if (!rnode) {
        errno = ENOENT;
        return -1;
    }
    if (rnode_free_idset (rnode, n->ids) < 0)
        return -1;
    rl->avail += idset_count (n->ids);
    return 0;
}

static int rlist_remove_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *rnode = rlist_find_rank (rl, n->rank);
    if (!rnode) {
        errno = ENOENT;
        return -1;
    }
    if (rnode_alloc_idset (rnode, n->avail) < 0)
        return -1;
    rl->avail -= idset_count (n->avail);
    return 0;
}

int rlist_free (struct rlist *rl, struct rlist *alloc)
{
    zlistx_t *freed = NULL;
    struct rnode *n = NULL;

    if (!(freed = zlistx_new ()))
        return -1;

    n = zlistx_first (alloc->nodes);
    while (n) {
        if (rlist_free_rnode (rl, n) < 0)
            goto cleanup;
        zlistx_add_end (freed, n);
        n = zlistx_next (alloc->nodes);
    }
    zlistx_destroy (&freed);
    return (0);
cleanup:
    /* re-allocate all freed items */
    n = zlistx_first (freed);
    while (n) {
        rlist_remove_rnode (rl, n);
        n = zlistx_next (freed);
    }
    zlistx_destroy (&freed);
    return (-1);
}

int rlist_remove (struct rlist *rl, struct rlist *alloc)
{
    zlistx_t *allocd = NULL;
    struct rnode *n = NULL;
    if (!alloc || !(allocd = zlistx_new ()))
        return -1;
    n = zlistx_first (alloc->nodes);
    while (n) {
        if (rlist_remove_rnode (rl, n) < 0)
            goto cleanup;
        zlistx_add_end (allocd, n);
        n = zlistx_next (alloc->nodes);
    }
    zlistx_destroy (&allocd);
    return 0;
cleanup:
    n = zlistx_first (allocd);
    while (n) {
        rlist_free_rnode (rl, n);
        n = zlistx_next (allocd);
    }
    zlistx_destroy (&allocd);
    return -1;
}

size_t rlist_nnodes (struct rlist *rl)
{
    return zlistx_size (rl->nodes);
}

/* vi: ts=4 sw=4 expandtab
 */
