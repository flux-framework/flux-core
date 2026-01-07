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

#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "rnode.h"
#include "match.h"
#include "rlist.h"
#include "rlist_private.h"

static int by_rank (const void *item1, const void *item2);

static size_t rank_hasher (const void *key)
{
    const int *id = key;
    return *id;
}

static int rank_hash_key_cmp (const void *key1, const void *key2)
{
    const int *a = key1;
    const int *b = key2;
    return *a - *b;
}

static zhashx_t *rank_hash_create (void)
{
    zhashx_t *hash;

    if (!(hash = zhashx_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    zhashx_set_key_hasher (hash, rank_hasher);
    zhashx_set_key_comparator (hash, rank_hash_key_cmp);
    zhashx_set_key_duplicator (hash, NULL);
    zhashx_set_key_destructor (hash, NULL);

    return hash;
}

static struct rnode * rank_hash_lookup (const struct rlist *rl, int rank)
{
    return zhashx_lookup (rl->rank_index, &rank);
}

static void rank_hash_delete (const struct rlist *rl, int rank)
{
    zhashx_delete (rl->rank_index, &rank);
}

static int rank_hash_insert (struct rlist *rl, struct rnode *n)
{
    return zhashx_insert (rl->rank_index, &n->rank, n);
}

static void rank_hash_purge (struct rlist *rl)
{
    zhashx_purge (rl->rank_index);
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

void rlist_destroy (struct rlist *rl)
{
    if (rl) {
        int saved_errno = errno;
        zlistx_destroy (&rl->nodes);
        zhashx_destroy (&rl->noremap);
        zhashx_destroy (&rl->rank_index);
        json_decref (rl->scheduling);
        free (rl);
        errno = saved_errno;
    }
}

static void rn_free_fn (void **x)
{
    if (x) {
        rnode_destroy (*(struct rnode **)x);
        *x = NULL;
    }
}

static void valfree (void **item)
{
    if (item) {
        free (*item);
        *item = NULL;
    }
}

struct rlist *rlist_create (void)
{
    struct rlist *rl = calloc (1, sizeof (*rl));
    if (!(rl->nodes = zlistx_new ()))
        goto err;
    zlistx_set_destructor (rl->nodes, rn_free_fn);

    if (!(rl->rank_index = rank_hash_create ())
        || !(rl->noremap = zhashx_new ()))
        goto err;
    zhashx_set_destructor (rl->noremap, valfree);
    zhashx_set_duplicator (rl->noremap, (zhashx_duplicator_fn *) strdup);
    zhashx_insert (rl->noremap, "gpu", "gpu");
    return (rl);
err:
    rlist_destroy (rl);
    return (NULL);
}

/*  Append two scheduling JSON objects s1 and s2.
 *
 *  These objects are supposed to be opaque, so for now we punt on
 *   doing an actual merge and just return s1 if non-NULL or s2 if non-NULL.
 *
 *  In the future, perhaps a "deep merge" could be done here instead, though
 *   the actual implementation of the scheduling key, JGF, do not make this
 *   easy since the main components are two lists of nodes and edges.
 */
static json_t * scheduling_key_append (json_t *s1, json_t *s2)
{
    if (s1)
        return json_incref (s1);
    else if (s1 == NULL && s2)
        return json_incref (s2);
    else
        return NULL;
}

static struct rnode *rlist_find_rank (const struct rlist *rl, uint32_t rank)
{
    return rank_hash_lookup (rl, rank);
}

static void rlist_update_totals (struct rlist *rl, struct rnode *n)
{
    rl->total += rnode_count (n);
    if (n->up)
        rl->avail += rnode_avail (n);
}

static int rlist_add_rnode_new (struct rlist *rl, struct rnode *n)
{
    void *handle;
    if (!(handle = zlistx_add_end (rl->nodes, n)))
        return -1;
    if (rank_hash_insert (rl, n) < 0)
        return -1;
    rlist_update_totals (rl, n);
    return 0;
}

/*  Add rnode 'n' to the rlist 'rl'. The memory for rnode n is stolen
 *   by this function (either the rnode is consumed by the rlist, or
 *   the rnode is destroyed after its resources are applied to 'rl')
 */
int rlist_add_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *found = rlist_find_rank (rl, n->rank);
    if (found) {
        if (rnode_add (found, n) < 0)
            return -1;
        rlist_update_totals (rl, n);
        rnode_destroy (n);
    }
    else if (rlist_add_rnode_new (rl, n) < 0)
        return -1;
    return 0;
}

typedef struct rnode * (*rnode_copy_f) (const struct rnode *, void *arg);

static struct rlist *rlist_copy_internal (const struct rlist *orig,
                                          rnode_copy_f cpfn,
                                          void *arg)
{
    struct rnode *n;
    struct rlist *rl = rlist_create ();
    if (!rl)
        return NULL;

    n = zlistx_first (orig->nodes);
    while (n) {
        struct rnode *copy = (*cpfn) (n, arg);
        if (copy && rlist_add_rnode_new (rl, copy) < 0) {
            rnode_destroy (copy);
            goto fail;
        }
        n = zlistx_next (orig->nodes);
    }

    /*  Copy entire opaque scheduling key unless rlist is empty
     */
    if (rlist_nnodes (rl) > 0)
        rl->scheduling = scheduling_key_append (orig->scheduling, NULL);


    /*  Copy noremap hash from original rlist
     */
    zhashx_destroy (&rl->noremap);
    rl->noremap = zhashx_dup (orig->noremap);
    if (!rl->noremap)
        return NULL;

    return rl;
fail:
    rlist_destroy (rl);
    return NULL;
}

static struct rnode *copy_empty (const struct rnode *rnode, void *arg)
{
    return rnode_copy_empty (rnode);
}

struct rlist *rlist_copy_empty (const struct rlist *orig)
{
    return rlist_copy_internal (orig, copy_empty, NULL);
}

static struct rnode *copy_alloc (const struct rnode *rnode, void *arg)
{
    return rnode_copy_alloc (rnode);
}

struct rlist *rlist_copy_allocated (const struct rlist *orig)
{
    return rlist_copy_internal (orig, copy_alloc, NULL);
}

static struct rnode *copy_cores (const struct rnode *rnode, void *arg)
{
    return rnode_copy_cores (rnode);
}

struct rlist *rlist_copy_cores (const struct rlist *orig)
{
    return rlist_copy_internal (orig, copy_cores, NULL);
}

struct rlist *rlist_copy_down (const struct rlist *orig)
{
    struct rnode *n;
    struct rlist *rl = rlist_create ();
    if (!rl)
        return NULL;
    n = zlistx_first (orig->nodes);
    while (n) {
        if (!n->up) {
            struct rnode *copy = rnode_copy_empty (n);
            if (!copy || rlist_add_rnode_new (rl, copy) < 0)
                goto fail;
        }
        n = zlistx_next (orig->nodes);
    }

    /*  Copy entire opaque scheduling key unless rlist is empty
     */
    if (rlist_nnodes (rl) > 0)
        rl->scheduling = scheduling_key_append (orig->scheduling, NULL);

    /*  Copy noremap hash from original rlist
     */
    zhashx_destroy (&rl->noremap);
    rl->noremap = zhashx_dup (orig->noremap);
    if (!rl->noremap)
        return NULL;

    return rl;
fail:
    rlist_destroy (rl);
    return NULL;
}

struct rlist * rlist_copy_ranks (const struct rlist *rl, struct idset *ranks)
{
    unsigned int i;
    struct rnode *n;
    struct rlist *result = rlist_create ();
    if (!result)
        return NULL;

    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if ((n = rlist_find_rank (rl, i))) {
            struct rnode *copy = rnode_copy (n);
            if (!copy || rlist_add_rnode_new (result, copy) < 0) {
                rnode_destroy (copy);
                goto err;
            }
        }
        i = idset_next (ranks, i);
    }

    /*  Copy entire opaque scheduling key unless rlist is empty
     */
    if (rlist_nnodes (result) > 0)
        result->scheduling = scheduling_key_append (rl->scheduling, NULL);

    /*  Copy noremap hash from original rlist
     */
    zhashx_destroy (&result->noremap);
    result->noremap = zhashx_dup (rl->noremap);
    if (!result->noremap)
        return NULL;

    return result;
err:
    rlist_destroy (result);
    return NULL;
}

struct rlist *rlist_copy_constraint (const struct rlist *orig,
                                     json_t *constraint,
                                     flux_error_t *errp)
{
    struct rlist *rl;
    struct job_constraint *jc;

    if (!(jc = job_constraint_create (constraint, errp))) {
        errno = EINVAL;
        return NULL;
    }
    rl = rlist_copy_internal (orig,
                              (rnode_copy_f) rnode_copy_match,
                              (void *) jc);
    job_constraint_destroy (jc);
    return rl;
}

struct rlist *rlist_copy_constraint_string (const struct rlist *orig,
                                            const char *constraint,
                                            flux_error_t *errp)
{
    struct rlist *rl;
    json_error_t error;
    json_t *o = json_loads (constraint, 0, &error);
    if (!o) {
        errprintf (errp, "%s", error.text);
        return NULL;
    }
    rl = rlist_copy_constraint (orig, o, errp);
    json_decref (o);
    return rl;
}

static int rlist_remove_rank (struct rlist *rl, int rank)
{
    void *handle;
    struct rnode *n;

    if (!(n = rank_hash_lookup (rl, rank))
        || !(handle = zlistx_find (rl->nodes, n))) {
        errno = ENOENT;
        return -1;
    }
    rank_hash_delete (rl, rank);
    zlistx_delete (rl->nodes, handle);
    return 0;
}

int rlist_remove_ranks (struct rlist *rl, const struct idset *ranks)
{
    int count = 0;
    unsigned int i;
    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if (rlist_remove_rank (rl, i) == 0)
            count++;
        i = idset_next (ranks, i);
    }
    return count;
}

int rlist_remap (struct rlist *rl)
{
    uint32_t rank = 0;
    struct rnode *n;

    rank_hash_purge (rl);

    /*   Sort list by ascending rank, then rerank starting at 0
     */
    zlistx_set_comparator (rl->nodes, by_rank);
    zlistx_sort (rl->nodes);

    n = zlistx_first (rl->nodes);
    while (n) {
        n->rank = rank++;
        if (rank_hash_insert (rl, n) < 0)
            return -1;
        if (rnode_remap (n, rl->noremap) < 0)
            return -1;
        n = zlistx_next (rl->nodes);
    }
    return 0;
}

struct rnode * rlist_find_host (const struct rlist *rl, const char *host)
{
    struct rnode *n = zlistx_first (rl->nodes);
    while (n) {
        if (n->hostname && streq (n->hostname, host))
            return n;
        n = zlistx_next (rl->nodes);
    }
    errno = ENOENT;
    return NULL;
}

static int rlist_rerank_hostlist (struct rlist *rl,
                                  struct hostlist *hl,
                                  flux_error_t *errp)
{
    uint32_t rank = 0;
    const char *host = hostlist_first (hl);
    while (host) {
        struct rnode *n = rlist_find_host (rl, host);
        if (!n) {
            errprintf (errp, "Host %s not found in resources", host);
            return -1;
        }
        n->rank = rank++;
        if (rank_hash_insert (rl, n) < 0) {
            errprintf (errp, "failed to hash rank %u", n->rank);
            return -1;
        }
        host = hostlist_next (hl);
    }
    return 0;
}

int rlist_rerank (struct rlist *rl, const char *hosts, flux_error_t *errp)
{
    int rc = -1;
    struct hostlist *hl = NULL;
    struct hostlist *orig = NULL;

    if (!(hl = hostlist_decode (hosts))) {
        errprintf (errp, "hostlist_decode: %s: %s", hosts, strerror (errno));
        return -1;
    }

    if (hostlist_count (hl) > rlist_nnodes (rl)) {
        errprintf (errp,
                   "Number of hosts (%d) is greater than node count (%zu)",
                   hostlist_count (hl),
                   rlist_nnodes (rl));
        errno = EOVERFLOW;
        goto done;
    }
    else if (hostlist_count (hl) < rlist_nnodes (rl)) {
            errprintf (errp,
                      "Number of hosts (%d) is less than node count (%zu)",
                      hostlist_count (hl),
                      rlist_nnodes (rl));
        errno = ENOSPC;
        goto done;
    }

    rank_hash_purge (rl);

    /* Save original rank mapping in case of undo
     */
    if (!(orig = rlist_nodelist (rl)))
        goto done;

    /* Perform re-ranking based on hostlist hl. On failure, undo
     *  by reranking with original hostlist.
     */
    if ((rc = rlist_rerank_hostlist (rl, hl, errp)) < 0) {
        int saved_errno = errno;
        (void) rlist_rerank_hostlist (rl, orig, NULL);
        errno = saved_errno;
    }
done:
    hostlist_destroy (orig);
    hostlist_destroy (hl);
    return rc;
}

static struct rnode *rlist_detach_rank (struct rlist *rl, uint32_t rank)
{
    struct rnode *n = rlist_find_rank (rl, rank);
    if (n) {
        zlistx_detach (rl->nodes, zlistx_find (rl->nodes, n));
        rank_hash_delete (rl, rank);
    }
    return n;
}

struct rlist *rlist_diff (const struct rlist *rla, const struct rlist *rlb)
{
    struct rnode *n;
    struct rlist *rl = rlist_create ();

    if (!rl || rlist_append (rl, rla) < 0) {
        rlist_destroy (rl);
        return NULL;
    }

    n = zlistx_first (rlb->nodes);
    while (n) {
        /*  Attempt to find and "detach" the rank which we're diffing.
         */
        struct rnode *na = rlist_detach_rank (rl, n->rank);
        if (na) {
            /*  Diff the individual resource node.
             *  If the result is empty, then do nothing since we've
             *   already detached this node from the list. O/w, push
             *   the result back onto the rlist.
             */
            struct rnode *result = rnode_diff (na, n);
            if (!rnode_empty (result))
                rlist_add_rnode (rl, result);
            else
                rnode_destroy (result);

            /*  Always need to free the detached rnode */
            rnode_destroy (na);
        }
        n = zlistx_next (rlb->nodes);
    }
    return rl;
}

struct rlist *rlist_union (const struct rlist *rla, const struct rlist *rlb)
{
    struct rlist *result = NULL;

    /*  First take the set difference of b from a, such that there are no
     *   common resources in 'rlb' and 'result'.
     */
    if (!(result = rlist_diff (rla, rlb)))
        return NULL;

    /*  Now append 'rlb' to 'result' to get the union of 'rla' + 'rlb':
     */
    if (rlist_append (result, rlb) < 0) {
        rlist_destroy (result);
        return NULL;
    }

    return result;
}

/*  Add resource set rlb to rla: rla becomes the union of a and b.
 */
int rlist_add (struct rlist *rla, const struct rlist *rlb)
{
    int rc;
    struct rlist *diff = NULL;

    /*  Take the set difference of a from b so there are no overlapping
     *   resources with rla in `diff`
     */
    if (!(diff = rlist_diff (rlb, rla)))
        return -1;

    /*  Now append diff to rla
     */
    rc = rlist_append (rla, diff);
    rlist_destroy (diff);
    return rc;
}

struct rlist *rlist_intersect (const struct rlist *rla,
                               const struct rlist *rlb)
{
    struct rnode *n;
    struct rlist *result = rlist_create ();

    if (!result)
        return NULL;

    n = zlistx_first (rlb->nodes);
    while (n) {
        struct rnode *na = rlist_find_rank (rla, n->rank);
        struct rnode *nx = rnode_intersect (na, n);
        if (nx != NULL
            && !rnode_empty (nx)
            && rlist_add_rnode (result, nx) < 0)
            goto err;
        n = zlistx_next (rlb->nodes);
    }

    /*  Copy opaque scheduling key unless result is empty
     */
    if (rlist_nnodes (result) > 0)
        result->scheduling = scheduling_key_append (rla->scheduling, NULL);

    return result;
err:
    rlist_destroy (result);
    return NULL;
}

static void free_item (void **x)
{
    if (x) {
        free (*x);
        *x = NULL;
    }
}

static zlistx_t *errlist_create ()
{
    zlistx_t *l = zlistx_new ();
    if (!l)
        return NULL;
    zlistx_set_destructor (l, free_item);
    return l;
}

static void errlist_destroy (zlistx_t *l)
{
    zlistx_destroy (&l);
}

static int errlist_append (zlistx_t *l, const char *fmt, ...)
{
    char *s = NULL;
    va_list ap;
    va_start (ap, fmt);
    if (vasprintf (&s, fmt, ap) < 0)
        return -1;
    va_end (ap);
    if (!zlistx_add_end (l, s)) {
        free (s);
        return -1;
    }
    return 0;
}

static int errlist_concat (zlistx_t *l, char *buf, size_t len)
{
    int n = 0;
    char *s;

    memset (buf, 0, len);

    s = zlistx_first (l);
    while (s) {
        if ((int)len - n > 0)
            strncpy (buf + n, s, len - n);
        n += strlen (s);
        s = zlistx_next (l);
        if (s) {
            strncat (buf, ", ", len - n);
            n += 2;
        }
    }
    return len;
}

static int rnode_namecmp (const void *s1, const void *s2)
{
    const char *a = s1;
    const char *b = s2;

    if (streq (a, "core"))
        return -1;
    else if (streq (b, "core"))
        return 1;
    else return strcmp (a, b);
}

static int rnode_sprintfcat (const struct rnode *n,
                             char **dest,
                             size_t *sizep,
                             size_t *lenp)
{
    int rc = -1;
    zlistx_t *keys = NULL;
    char *ids = NULL;
    char *name = NULL;
    char *comma = "";

    /*  Generate sorted list of resources on those ranks.
     *  (Ensuring that "core" is always first)
     */
    keys = zhashx_keys (n->children);
    zlistx_set_comparator (keys, rnode_namecmp);
    zlistx_sort (keys);

    /*  Output all resource strings as name[ids]
     */
    name = zlistx_first (keys);
    while (name) {
        struct rnode_child *c = zhashx_lookup (n->children, name);

        /*  Skip empty sets */
        if (idset_count (c->avail) > 0) {
            ids = idset_encode (c->avail,
                                IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS);
            if (!ids)
                goto fail;
            if (sprintfcat (dest, sizep, lenp,
                            "%s%s%s",
                            comma,
                            c->name,
                            ids) < 0)
                goto fail;

            free (ids);
            ids = NULL;
            comma = ",";
        }
        name = zlistx_next (keys);
    }
    rc = 0;
fail:
    zlistx_destroy (&keys);
    free (ids);
    return rc;
}

static char * rnode_child_dumps (struct rnode *rnode)
{
    size_t n = 0;
    size_t len = 0;
    char *s = NULL;
    if (rnode_sprintfcat (rnode, &s, &n, &len) < 0) {
        free (s);
        return NULL;
    }
    return s;
}

static int ignore_mask_missing (enum rlist_verify_mode core_mode,
                                enum rlist_verify_mode gpu_mode)
{
    int rnode_ignore = 0;

    if (gpu_mode == RLIST_VERIFY_IGNORE
        || gpu_mode == RLIST_VERIFY_ALLOW_MISSING)
        rnode_ignore |= RNODE_IGNORE_GPU;
    if (core_mode == RLIST_VERIFY_IGNORE
        || core_mode == RLIST_VERIFY_ALLOW_MISSING)
        rnode_ignore |= RNODE_IGNORE_CORE;

    return rnode_ignore;
}

static int ignore_mask_extra (enum rlist_verify_mode core_mode,
                              enum rlist_verify_mode gpu_mode)
{
    int rnode_ignore = 0;

    if (gpu_mode == RLIST_VERIFY_IGNORE
        || gpu_mode == RLIST_VERIFY_ALLOW_EXTRA)
        rnode_ignore |= RNODE_IGNORE_GPU;
    if (core_mode == RLIST_VERIFY_IGNORE
        || core_mode == RLIST_VERIFY_ALLOW_EXTRA)
        rnode_ignore |= RNODE_IGNORE_CORE;

    return rnode_ignore;
}

int rlist_verify_ex (flux_error_t *errp,
                     const struct rlist *expected,
                     const struct rlist *rl,
                     struct rlist_verify_config *config)
{
    struct rnode *n = NULL;
    struct rnode *exp = NULL;
    struct rnode *diff = NULL;
    zlistx_t *errors = NULL;
    int saved_errno;
    int rc = -1;

    enum rlist_verify_mode hostname_mode =
        rlist_verify_config_get_mode (config, "hostname");
    enum rlist_verify_mode core_mode =
        rlist_verify_config_get_mode (config, "core");
    enum rlist_verify_mode gpu_mode =
        rlist_verify_config_get_mode (config, "gpu");

    if (!(errors = errlist_create ())) {
        errprintf (errp, "Internal error: Out of memory");
        errno = ENOMEM;
        goto done;
    }

    if (rlist_nnodes (rl) != 1) {
        errlist_append (errors,
                        "Verification supported on single rank only");
        errno = EINVAL;
        goto done;
    }

    n = zlistx_first (rl->nodes);
    if (!(exp = rlist_find_rank (expected, n->rank))) {
        errlist_append (errors,
                        "rank %d not found in expected ranks",
                        n->rank);
        errno = EINVAL;
        goto done;
    }
    if (hostname_mode == RLIST_VERIFY_STRICT
        && rnode_hostname_cmp (n, exp) != 0) {
        errlist_append (errors,
                        "rank %d got hostname '%s', expected '%s'",
                        n->rank,
                        n->hostname ? n->hostname : "unknown",
                        exp->hostname ? exp->hostname : "unknown");
        goto done;
    }

    /* Check for missing resources by comparing expected to actual:
     */
    if (!(diff = rnode_diff_ex (exp,
                                n,
                                ignore_mask_missing (core_mode, gpu_mode)))) {
        errlist_append (errors,
                        "Internal error: rnode_diff failed: %s",
                        strerror (errno));
        goto done;
    }
    if (!rnode_empty (diff)) {
        char *s = rnode_child_dumps (diff);
        errlist_append (errors,
                        "rank %d (%s) missing resources: %s",
                        n->rank, n->hostname ? n->hostname : "unknown", s);
        free (s);
        goto done;
    }
    rnode_destroy (diff);

    /* Check for extra resources by comparing actual to expected:
     */
    if (!(diff = rnode_diff_ex (n,
                                exp,
                                ignore_mask_extra (core_mode, gpu_mode)))) {
        errlist_append (errors,
                        "Internal error: rnode_diff failed: %s",
                        strerror (errno));
        goto done;
    }
    if (rnode_empty (diff))
        rc = 0;
    else {
        char *s = rnode_child_dumps (diff);
        errlist_append (errors,
                        "rank %d (%s) has extra resources: %s",
                        n->rank, n->hostname ? n->hostname : "unknown", s);
        free (s);
        rc = 1;
    }
done:
    saved_errno = 0;
    rnode_destroy (diff);
    memset (errp->text, 0, sizeof (errp->text));
    if (errors) {
        errlist_concat (errors, errp->text, sizeof (errp->text));
        errlist_destroy (errors);
    }
    errno = saved_errno;
    return rc;
}

int rlist_verify (flux_error_t *errp,
                   const struct rlist *expected,
                   const struct rlist *rl)
{
    return rlist_verify_ex (errp, expected, rl, NULL);
}

int rlist_append (struct rlist *rl, const struct rlist *rl2)
{
    json_t *o;
    struct rnode *n = zlistx_first (rl2->nodes);
    while (n) {
        struct rnode *copy = rnode_copy_avail (n);
        if (!copy || rlist_add_rnode (rl, copy) < 0) {
            rnode_destroy (copy);
            return -1;
        }
        n = zlistx_next (rl2->nodes);
    }

    o = scheduling_key_append (rl->scheduling, rl2->scheduling);
    json_decref (rl->scheduling);
    rl->scheduling = o;

    return 0;
}

static int rlist_append_rank (struct rlist *rl,
                              const char *hostname,
                              unsigned int rank,
                              json_t *children)
{
    struct rnode *n = rnode_create_children (hostname, rank, children);
    if (!n || rlist_add_rnode (rl, n) < 0) {
        rnode_destroy (n);
        return -1;
    }
    return 0;
}

int rlist_append_rank_cores (struct rlist *rl,
                             const char *hostname,
                             unsigned int rank,
                             const char *core_ids)
{
    int rc;
    json_t *children = json_pack ("{s:s}", "core", core_ids);
    if (!children)
        return -1;
    rc = rlist_append_rank (rl, hostname, rank, children);
    json_decref (children);
    return rc;
}

int rlist_rank_add_child (struct rlist *rl,
                          unsigned int rank,
                          const char *name,
                          const char *ids)
{
    struct rnode *n = rlist_find_rank (rl, rank);
    if (!n) {
        errno = ENOENT;
        return -1;
    }
    if (rnode_add_child (n, name, ids) == NULL)
        return -1;
    return 0;
}

static int rlist_append_ranks (struct rlist *rl,
                               const char *rank,
                               json_t *children)
{
    int rc = -1;
    unsigned int i;
    struct idset * ranks = idset_decode (rank);
    if (!ranks)
        return -1;
    i = idset_first (ranks);
    while (i != IDSET_INVALID_ID) {
        if (rlist_append_rank (rl, NULL, i, children) < 0)
            goto err;
        i = idset_next (ranks, i);
    }
    rc = 0;
err:
    idset_destroy (ranks);
    return rc;
}

static int rlist_append_cores (struct rlist *rl,
                               const char *hostname,
                               int rank,
                               struct idset *idset)
{
    struct rnode *n = rnode_create_idset (hostname, rank, idset);
    if (!n || rlist_add_rnode (rl, n) < 0) {
        rnode_destroy (n);
        return -1;
    }
    return 0;
}

static int rlist_append_rank_entry (struct rlist *rl,
                                    json_t *entry,
                                    json_error_t *ep)
{
    const char *ranks;
    json_t *children;
    if (json_unpack_ex (entry, ep, 0,
                        "{s:s s:o}",
                        "rank", &ranks,
                        "children", &children) < 0) {
        return -1;
    }
    return rlist_append_ranks (rl, ranks, children);
}

static struct hostlist * hostlist_from_array (json_t *o)
{
    size_t index;
    json_t *val;

    struct hostlist *hl = hostlist_create ();
    if (hl == NULL)
        return NULL;

    json_array_foreach (o, index, val) {
        const char *hosts = json_string_value (val);
        if (hostlist_append (hl, hosts) < 0)
            goto err;
    }

    return hl;
err:
    hostlist_destroy (hl);
    return NULL;
}

static int rlist_assign_hostlist (struct rlist *rl, struct hostlist *hl)
{
    struct rnode *n;

    if (!hl || hostlist_count (hl) != zlistx_size (rl->nodes))
        return -1;

    /*  Reset default sort to order nodes by "rank" */
    zlistx_set_comparator (rl->nodes, by_rank);
    zlistx_sort(rl->nodes);

    /*  Consume a hostname for each node in the rlist */
    n = zlistx_first (rl->nodes);
    (void) hostlist_first (hl);
    while (n) {
        free (n->hostname);
        if (!(n->hostname = strdup (hostlist_current (hl))))
            return -1;
        (void) hostlist_next (hl);
        n = zlistx_next (rl->nodes);
    }
    return 0;
}

int rlist_assign_hosts (struct rlist *rl, const char *hosts)
{
    int rc;
    struct hostlist *hl = hostlist_decode (hosts);
    rc = rlist_assign_hostlist (rl, hl);
    hostlist_destroy (hl);
    return rc;
}

static int rlist_assign_nodelist (struct rlist *rl, json_t *nodelist)
{
    int rc = -1;
    struct hostlist *hl = hostlist_from_array (nodelist);
    rc = rlist_assign_hostlist (rl, hl);
    hostlist_destroy (hl);
    return rc;
}

static void property_destructor (void **arg)
{
    if (arg) {
        idset_destroy (*(struct idset **) arg);
        *arg = NULL;
    }
}

static const char * property_string_invalid (const char *s)
{
    return strpbrk (s, "^&'\"`|()");
}

int rlist_add_property (struct rlist *rl,
                        flux_error_t *errp,
                        const char *name,
                        const char *targets)
{
    unsigned int i;
    int count;
    struct rnode *n;
    struct idset *ids = NULL;
    struct idset *unknown = NULL;
    int rc = -1;
    char *p;
    const char *invalid;

    if ((invalid = property_string_invalid (name))) {
        errprintf (errp,
                   "Invalid character '%c' in property \"%s\"",
                   *invalid,
                   name);
        errno = EINVAL;
        goto out;
    }

    if (!targets || !(ids = idset_decode (targets))) {
        errprintf (errp,
                   "Invalid idset string '%s'",
                   targets ? targets : "(null)");
        errno = EINVAL;
        goto out;
    }

    /*  Check for invalid ranks first, so we can fail early before
     *   applying any properties.
     */
    if (!(unknown = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp, "Out of memory");
        goto out;
    }
    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (!rlist_find_rank (rl, i)
            && idset_set (unknown, i) < 0) {
            errprintf (errp, "unknown rank %u", i);
            errno = ENOENT;
            goto out;
        }
        i = idset_next (ids, i);
    }
    if ((count = idset_count (unknown)) > 0) {
        p = idset_encode (unknown, IDSET_FLAG_RANGE);
        errprintf (errp,
                   "%s%s not found in target resource list",
                   p ? (count == 1 ? "rank " : "ranks ") : "some ranks",
                   p ? p : "");
        free (p);
        errno = ENOENT;
        goto out;
    }

    i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if ((n = rlist_find_rank (rl, i)))
            if (rnode_set_property (n, name) < 0) {
                errprintf (errp,
                           "Failed to set property %s on rank %u",
                           name,
                           i);
                goto out;
            }
        i = idset_next (ids, i);
    }
    rc = 0;
out:
    idset_destroy (ids);
    idset_destroy (unknown);
    return rc;
}

int rlist_assign_properties (struct rlist *rl,
                             json_t *properties,
                             flux_error_t *errp)
{
    json_t *val;
    const char *name;
    const char *invalid;
    int rc = -1;

    if (!rl || !properties) {
        errprintf (errp, "Invalid argument");
        errno = EINVAL;
        return -1;
    }

    if (!json_is_object (properties)) {
        errprintf (errp, "properties must be an object");
        errno = EINVAL;
        return -1;
    }

    /*  First, iterate all property inputs to ensure they are valid.
     *  This avoids the need to undo any applied properties on failure.
     */
    json_object_foreach (properties, name, val) {
        struct idset *ids;
        if (!json_is_string (val)) {
            char *s = json_dumps (val, JSON_COMPACT | JSON_ENCODE_ANY);
            errprintf (errp, "properties value '%s' not a string", s);
            free (s);
            errno = EINVAL;
            goto error;
        }
        if ((invalid = property_string_invalid (name))) {
            errprintf (errp,
                       "invalid character '%c' in property \"%s\"",
                        *invalid,
                        name);
            errno = EINVAL;
            goto error;
        }
        if (!(ids = idset_decode (json_string_value (val)))) {
            errprintf (errp,
                       "invalid idset '%s' specified for property \"%s\"",
                       json_string_value (val),
                       name);
            errno = EINVAL;
            goto error;
        }
        idset_destroy (ids);
    }

    json_object_foreach (properties, name, val) {
        /*  Note: validity of json_string_value (val)
         *   checked above, no need to do it again here.
         */
        if (rlist_add_property (rl, errp, name, json_string_value (val)) < 0)
            goto error;
    }
    rc = 0;
error:
    return rc;
}

struct rlist *rlist_from_json (json_t *o, json_error_t *errp)
{
    int i, version;
    struct rlist *rl = NULL;
    json_t *entry = NULL;
    json_t *R_lite = NULL;
    json_t *nodelist = NULL;
    json_t *scheduling = NULL;
    json_t *properties = NULL;
    int nslots = -1;
    double starttime = -1.;
    double expiration = -1.;
    flux_error_t error;

    if (json_unpack_ex (o, errp, 0,
                        "{s:i s?O s:{s:o s?o s?o s?i s?F s?F}}",
                        "version", &version,
                        "scheduling", &scheduling,
                        "execution",
                          "R_lite", &R_lite,
                          "nodelist", &nodelist,
                          "properties", &properties,
                          "nslots", &nslots,
                          "starttime", &starttime,
                          "expiration", &expiration) < 0)
        goto err;
    if (version != 1) {
        if (errp)
            snprintf (errp->text,
                      sizeof (errp->text),
                      "invalid version=%d",
                      version);
        goto err;
    }
    if (!(rl = rlist_create ()))
        goto err;

    if (scheduling)
        rl->scheduling = scheduling;
    if (nslots > 0)
        rl->nslots = nslots;
    if (starttime > 0.)
        rl->starttime = starttime;
    if (expiration > 0.)
        rl->expiration = expiration;

    json_array_foreach (R_lite, i, entry) {
        if (rlist_append_rank_entry (rl, entry, errp) < 0)
            goto err;
    }
    if (nodelist && rlist_assign_nodelist (rl, nodelist) < 0)
        goto err;
    if (properties && rlist_assign_properties (rl, properties, &error) < 0) {
        if (errp)
            snprintf (errp->text, sizeof (errp->text), "%s", error.text);
        goto err;
    }
    return (rl);
err:
    rlist_destroy (rl);
    return (NULL);
}

struct rlist *rlist_from_R (const char *s)
{
    json_error_t err;
    struct rlist *rl = NULL;
    json_t *o = json_loads (s, 0, &err);
    if (o)
        rl = rlist_from_json (o, &err);
    json_decref (o);
    return rl;
}

/* Helper for rlist_compressed */
struct multi_rnode {
    struct idset *ids;
    const struct rnode *rnode;
};

static int multi_rnode_cmp (struct multi_rnode *x, const struct rnode *n)
{
    int rv = rnode_cmp (x->rnode, n);

    /* Only collapse nodes with same avail idset + same up/down status */
    if (rv == 0 && n->up == x->rnode->up)
        return 0;

    /* O/w, order doesn't matter too much, but put up nodes first */
    return n->up ? -1 : 1;
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
    return rnode_encode (mrn->rnode, mrn->ids);
}


static int multi_rnode_by_rank (const void *item1, const void *item2)
{
    const struct multi_rnode *mrn1 = item1;
    const struct multi_rnode *mrn2 = item2;

    unsigned int x = idset_first (mrn1->ids);
    unsigned int y = idset_first (mrn2->ids);

    return (x - y);
}

static zlistx_t * rlist_mrlist (const struct rlist *rl)
{
    struct rnode *n = NULL;
    struct multi_rnode *mrn = NULL;
    zlistx_t *l = zlistx_new ();

    zlistx_set_comparator (l, (zlistx_comparator_fn *) multi_rnode_cmp);
    zlistx_set_destructor (l, (zlistx_destructor_fn *) multi_rnode_destroy);

    n = zlistx_first (rl->nodes);
    while (n) {
        if (zlistx_find (l, n)) {
            if (!(mrn = zlistx_handle_item (zlistx_cursor (l)))
                    || idset_set (mrn->ids, n->rank) < 0) {
                goto fail;
            }
        }
        else {
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

static json_t * rlist_compressed (const struct rlist *rl)
{
    struct multi_rnode *mrn = NULL;
    json_t *o = json_array ();
    zlistx_t *l = rlist_mrlist (rl);

    if (!l)
        return NULL;
    zlistx_set_comparator (l, (zlistx_comparator_fn *) multi_rnode_by_rank);
    zlistx_sort (l);
    mrn = zlistx_first (l);
    while (mrn) {
        if (rnode_avail_total (mrn->rnode) > 0) {
            json_t *entry = multi_rnode_tojson (mrn);
            if (!entry || json_array_append_new (o, entry) != 0) {
                json_decref (entry);
                goto fail;
            }
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

static int mrnode_sprintfcat (struct multi_rnode *mrn,
                              char **resultp,
                              size_t *sizep,
                              size_t *lenp)
{
    int flags = IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS;
    int rc = -1;
    char *ranks = NULL;

    /* Do not output anything if there are no available resources */
    if (rnode_avail_total (mrn->rnode) == 0)
        return 0;

    /*  First encode set of ranks into a string:
     */
    if (!(ranks = idset_encode (mrn->ids, flags)))
        goto fail;
    if (sprintfcat (resultp, sizep, lenp,
                    "%srank%s/",
                    (*resultp)[0] != '\0' ? " ": "",
                    ranks) < 0)
        goto fail;

    if (rnode_sprintfcat (mrn->rnode, resultp, sizep, lenp) < 0)
        goto fail;

    rc = 0;
fail:
    free (ranks);
    return rc;
}

char * rlist_dumps (const struct rlist *rl)
{
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
        if (mrnode_sprintfcat (mrn, &result, &size, &len) < 0)
            goto fail;
        mrn = zlistx_next (l);
    }
    zlistx_destroy (&l);
    return (result);
fail:
    free (result);
    zlistx_destroy (&l);
    return NULL;
}

static json_t *hostlist_to_nodelist (struct hostlist *hl)
{
    json_t *o = NULL;
    char *hosts;
    if ((hosts = hostlist_encode (hl)))
        o = json_pack ("[s]", hosts);
    free (hosts);
    return o;
}

struct idset *rlist_ranks (const struct rlist *rl)
{
    struct rnode *n;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);
    if (!ids)
        return NULL;

    n = zlistx_first (rl->nodes);
    while (n) {
        if (idset_set (ids, n->rank) < 0)
            goto fail;
        n = zlistx_next (rl->nodes);
    }
    return ids;
fail:
    idset_destroy (ids);
    return NULL;
}

struct hostlist *rlist_nodelist (const struct rlist *rl)
{
    struct rnode *n;
    struct hostlist *hl = hostlist_create ();

    if (!hl)
        return NULL;

    /*  List must be sorted by rank before collecting nodelist
     */
    zlistx_set_comparator (rl->nodes, by_rank);
    zlistx_sort (rl->nodes);

    n = zlistx_first (rl->nodes);
    while (n) {
        if (!n->hostname || hostlist_append (hl, n->hostname) < 0)
            goto fail;
        n = zlistx_next (rl->nodes);
    }
    return hl;
fail:
    hostlist_destroy (hl);
    return NULL;
}

static int rlist_idset_set_by_host (const struct rlist *rl,
                                    struct idset *ids,
                                    const char *host)
{
    int count = 0;
    struct rnode *n = zlistx_first (rl->nodes);
    while (n) {
        if (n->hostname && streq (n->hostname, host)) {
            if (idset_set (ids, n->rank) < 0)
                return -1;
            count++;
        }
        n = zlistx_next (rl->nodes);
    }
    return count;
}

struct idset *rlist_hosts_to_ranks (const struct rlist *rl,
                                    const char *hosts,
                                    flux_error_t *errp)
{
    const char *host;
    struct idset *ids = NULL;
    struct hostlist *hl = NULL;
    struct hostlist *missing = NULL;

    if (errp)
        memset (errp->text, 0, sizeof (errp->text));

    if (rl == NULL || hosts == NULL) {
        errprintf (errp, "An expected argument was NULL");
        errno = EINVAL;
        return NULL;
    }
    if (!(hl = hostlist_decode (hosts))) {
        errprintf (errp, "Hostlist cannot be decoded");
        goto fail;
    }
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp, "idset_create: %s", strerror (errno));
        goto fail;
    }
    if (!(missing = hostlist_create ())) {
        errprintf (errp, "hostlist_create: %s", strerror (errno));
        goto fail;
    }
    host = hostlist_first (hl);
    while (host) {
        int count = rlist_idset_set_by_host (rl, ids, host);
        if (count < 0) {
            errprintf (errp,
                        "error adding host %s to idset: %s",
                        host,
                        strerror (errno));
            goto fail;
        } else if (!count && hostlist_append (missing, host) < 0) {
            errprintf (errp,
                        "failed to append missing host '%s'",
                        host);
            goto fail;
        }
        host = hostlist_next (hl);
    }
    if (hostlist_count (missing)) {
        char *s = hostlist_encode (missing);
        errprintf (errp, "invalid hosts: %s", s ? s : "");
        free (s);
        goto fail;
    }
    hostlist_destroy (hl);
    hostlist_destroy (missing);
    return ids;
fail:
    hostlist_destroy (hl);
    hostlist_destroy (missing);
    idset_destroy (ids);
    return NULL;
}

int rlist_json_nodelist (const struct rlist *rl, json_t **result)
{
    struct hostlist *hl = rlist_nodelist (rl);
    if (!hl)
        return 0;
    *result = hostlist_to_nodelist (hl);
    hostlist_destroy (hl);
    return 0;
}

static zhashx_t *rlist_properties (const struct rlist *rl)
{
    int saved_errno;
    struct rnode *n;
    zlistx_t *keys = NULL;
    zhashx_t *properties = zhashx_new ();

    if (!properties) {
        errno = ENOMEM;
        return NULL;
    }

    zhashx_set_destructor (properties, property_destructor);

    n = zlistx_first (rl->nodes);
    while (n) {
        if (n->properties) {
            const char *name;
            if (!(keys = zhashx_keys (n->properties))) {
                errno = ENOMEM;
                goto error;
            }
            name = zlistx_first (keys);
            while (name) {
                struct idset *ids = zhashx_lookup (properties, name);
                if (!ids) {
                    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW))) {
                        errno = ENOMEM;
                        goto error;
                    }
                    (void)zhashx_insert (properties, name, ids);
                }
                if (idset_set (ids, n->rank) < 0)
                    goto error;
                name = zlistx_next (keys);
            }
            zlistx_destroy (&keys);
            keys = NULL;
        }
        n = zlistx_next (rl->nodes);
    }

    return properties;
error:
    saved_errno = errno;
    zhashx_destroy (&properties);
    zlistx_destroy (&keys);
    errno = saved_errno;
    return NULL;
}

static int rlist_json_properties (const struct rlist *rl, json_t **result)
{
    int saved_errno;
    int rc = -1;
    zhashx_t *properties = NULL;;
    json_t *o = NULL;
    struct idset *ids;

    if (!rl || !result) {
        errno = EINVAL;
        return -1;
    }

    if (!(properties = rlist_properties (rl)))
        return -1;

    /*  Do not bother returning an empty JSON object
     */
    if (zhashx_size (properties) == 0) {
        rc = 0;
        goto out;
    }

    if (!(o = json_object ())) {
        errno = ENOMEM;
        goto out;
    }
    ids = zhashx_first (properties);
    while (ids) {
        const char *name = zhashx_cursor (properties);
        char *s = idset_encode (ids, IDSET_FLAG_RANGE);
        json_t *val = NULL;
        if (!s
            || !(val = json_string (s))
            || json_object_set_new (o, name, val) < 0) {
            free (s);
            json_decref (val);
            errno = ENOMEM;
            goto out;
        }
        free (s);
        ids = zhashx_next (properties);
    }
    /*  Assign o to result, but incref since we decref in exit path
     */
    json_incref (o);
    *result = o;
    rc = 0;
out:
    saved_errno = errno;
    zhashx_destroy (&properties);
    json_decref (o);
    errno = saved_errno;
    return rc;
}

char *rlist_properties_encode (const struct rlist *rl)
{
    char *result = NULL;
    json_t *o = NULL;

    if (rlist_json_properties (rl, &o) < 0)
        return NULL;
    if (o == NULL)
        return (strdup ("{}"));
    result = json_dumps (o, 0);
    json_decref (o);
    return result;
}

json_t *rlist_to_R (const struct rlist *rl)
{
    json_t *R = NULL;
    json_t *R_lite = NULL;
    json_t *nodelist = NULL;
    json_t *properties = NULL;
    json_t *nslots = NULL;

    if (!rl)
        return NULL;

    /*  Reset default sort to order nodes by "rank" */
    zlistx_set_comparator (rl->nodes, by_rank);
    zlistx_sort (rl->nodes);

    if (!(R_lite = rlist_compressed (rl)))
        goto fail;

    if (rlist_json_nodelist (rl, &nodelist) < 0
        || rlist_json_properties (rl, &properties) < 0)
        goto fail;

    if (rl->nslots > 0) {
        nslots = json_integer (rl->nslots);
    }

    if (!(R = json_pack ("{s:i, s:{s:o s:o* s:f s:f}}",
                         "version", 1,
                         "execution",
                           "R_lite", R_lite,
                           "nslots", nslots,
                           "starttime", rl->starttime,
                           "expiration", rl->expiration)))
        goto fail;
    if (nodelist
        && json_object_set_new (json_object_get (R, "execution"),
                                "nodelist", nodelist) < 0)
        goto fail;
    if (properties
        && json_object_set_new (json_object_get (R, "execution"),
                                "properties", properties) < 0)
        goto fail;
    if (rl->scheduling
        && json_object_set (R, "scheduling", rl->scheduling) < 0)
        goto fail;

    return (R);
fail:
    json_decref (R);
    json_decref (nodelist);
    return NULL;
}

char *rlist_encode (const struct rlist *rl)
{
    json_t *o;
    char *R;
    if (!rl)
        return NULL;
    if (!(o = rlist_to_R (rl)))
        return NULL;
    R = json_dumps (o, 0);
    json_decref (o);
    return R;
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
    if (x->up != y->up)
        n = x->up ? -1 : 1;
    else if ((n = rnode_avail (y) - rnode_avail (x)) == 0)
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
        rc = rlist_append_cores (result, n->hostname, n->rank, ids);
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
        if (n == NULL) {
            errno = ENOSPC;
            goto err;
        }
        if (n->up) {
            if (!zlistx_add_end (l, n))
                goto err;
            nnodes--;
        }
        n = zlistx_next (rl->nodes);
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
static struct rlist *rlist_alloc_nnodes (struct rlist *rl,
                                         const struct rlist_alloc_info *ai)
{
    struct rlist *result = NULL;
    struct rnode *n = NULL;
    zlistx_t *cl = NULL;
    int slots = ai->nslots;

    if (rlist_nnodes (rl) < ai->nnodes) {
        errno = ENOSPC;
        return NULL;
    }
    if (ai->nslots < ai->nnodes) {
        errno = EINVAL;
        return NULL;
    }
    if (!(result = rlist_create ()))
        return NULL;

    /* 1. sort rank list by used cores ascending:
     */
    zlistx_set_comparator (rl->nodes, by_used);
    zlistx_sort (rl->nodes);

    if (ai->exclusive) {
        int nleft = ai->nnodes;
        struct rnode *cpy;
        n = zlistx_first (rl->nodes);
        while (n && nleft) {
            /*
             *  We can abort after we find the first non-idle or down node:
             *
             *  Note: by_used() sorts down nodes to back of list and
             *   rnode_avail() returns 0 if node is not up
             */
            if (rnode_avail (n) < rnode_count (n))
                goto unwind;

            if (!(cpy = rnode_copy (n))
                || rlist_add_rnode_new (result, cpy) < 0) {
                rnode_destroy (cpy);
                goto unwind;
            }
            rnode_alloc_idset (n, n->cores->ids);
            nleft--;
            n = zlistx_next (rl->nodes);
        }
        if (nleft) /* Unable to allocate all nodes exclusively */
            goto unwind;
        return result;
    }

    /* 2. get a list of the first up n nodes
     */
    if (!(cl = rlist_get_nnodes (rl, ai->nnodes)))
        goto unwind;

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
        if (rlist_rnode_alloc (rl, n, ai->slot_size, &ids) < 0)
            goto unwind;
        rc = rlist_append_cores (result, n->hostname, n->rank, ids);
        idset_destroy (ids);
        if (rc < 0)
            goto unwind;

        /*  If a node is empty, remove it from consideration.
         *  O/w, force it to the back of the list to ensure all N
         *   nodes are considered at least once.
         */
        zlistx_reorder (cl, zlistx_cursor (cl), false);
        if (rnode_avail (n) == 0)
            zlistx_detach (cl, zlistx_cursor (cl));
        else
            zlistx_move_end (cl, zlistx_cursor (cl));
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

static struct rlist *rlist_try_alloc (struct rlist *rl,
                                      const struct rlist_alloc_info *ai)
{
    struct rlist *result = NULL;
    const char *mode = ai->mode;

    if (!rl || !ai) {
        errno = EINVAL;
        return NULL;
    }

    /*  Reset default sort to order nodes by "rank" */
    zlistx_set_comparator (rl->nodes, by_rank);

    if (ai->nnodes > 0)
        result = rlist_alloc_nnodes (rl, ai);
    else if (mode == NULL || streq (mode, "worst-fit"))
        result = rlist_alloc_worst_fit (rl, ai->slot_size, ai->nslots);
    else if (mode && streq (mode, "best-fit"))
        result = rlist_alloc_best_fit (rl, ai->slot_size, ai->nslots);
    else if (mode && streq (mode, "first-fit"))
        result = rlist_alloc_first_fit (rl, ai->slot_size, ai->nslots);
    else
        errno = EINVAL;

    if (result)
        result->nslots = ai->nslots;
    return result;
}

/*  Determine if allocation request is feasible for rlist `rl`.
 */
static bool rlist_alloc_feasible (const struct rlist *rl, const char *mode,
                                  int nnodes, int slots, int slotsz)
{
    bool rc = false;
    struct rlist *result = NULL;
    struct rlist_alloc_info ai = {
        .nnodes = nnodes,
        .slot_size = slotsz,
        .nslots = slots,
        .mode = mode,
    };
    int saved_errno = errno;
    struct rlist *all = rlist_copy_empty (rl);
    if (all && (result = rlist_try_alloc (all, &ai)))
        rc = true;
    rlist_destroy (all);
    rlist_destroy (result);
    errno = saved_errno;
    return rc;
}

static int alloc_info_check (struct rlist *rl,
                             const struct rlist_alloc_info *ai,
                             flux_error_t *errp)
{
    int slots = ai->nslots;
    int nnodes = ai->nnodes;
    int slotsz = ai->slot_size;
    int total = slots * slotsz;

    if (slots <= 0 || slotsz <= 0 || nnodes < 0) {
        errno = EINVAL;
        return -1;
    }
    if (ai->exclusive && ai->nnodes <= 0) {
        errprintf (errp, "exclusive allocation only supported with nnodes");
        errno = EINVAL;
        return -1;
    }
    if (total > rl->total) {
        errprintf (errp, "unsatisfiable request");
        errno = EOVERFLOW;
        return -1;
    }
    if (total > rl->avail) {
        if (!rlist_alloc_feasible (rl,
                                   ai->mode,
                                   ai->nnodes,
                                   ai->nslots,
                                   ai->slot_size)) {
            errprintf (errp, "unsatisfiable request");
            errno = EOVERFLOW;
        }
        else
            errno = ENOSPC;
        return -1;
    }
    return 0;
}

static struct rlist *
rlist_alloc_constrained (struct rlist *rl,
                         const struct rlist_alloc_info *ai,
                         flux_error_t *errp)
{
    struct rlist *result;
    struct rlist *cpy;
    int saved_errno;

    if (!(cpy = rlist_copy_constraint (rl, ai->constraints, errp)))
        return NULL;

    if (rlist_count (cpy, "core") == 0) {
        errprintf (errp, "no resources satisfy provided constraints");
        errno = EOVERFLOW;
    }

    result = rlist_try_alloc (cpy, ai);
    saved_errno = errno;

    if (!result && errno == ENOSPC) {
        if (!rlist_alloc_feasible (cpy,
                                   ai->mode,
                                   ai->nnodes,
                                   ai->nslots,
                                   ai->slot_size)) {
            saved_errno = EOVERFLOW;
            errprintf (errp, "unsatisfiable constrained request");
        }
    }
    rlist_destroy (cpy);

    if (result && rlist_set_allocated (rl, result) < 0) {
        errprintf (errp, "rlist_set_allocated: %s", strerror (errno));
        rlist_destroy (result);
        result = NULL;
    }

    errno = saved_errno;
    return result;
}

struct rlist *rlist_alloc (struct rlist *rl,
                           const struct rlist_alloc_info *ai,
                           flux_error_t *errp)
{
    struct rlist *result = NULL;

    if (!rl || !ai) {
        errno = EINVAL;
        errprintf (errp, "Invalid argument");
        return NULL;
    }

    if (alloc_info_check (rl, ai, errp) < 0)
        return NULL;

    if (ai->constraints)
        result = rlist_alloc_constrained (rl, ai, errp);
    else {
        result = rlist_try_alloc (rl, ai);
        if (!result)
            errprintf (errp, "%s", strerror (errno));

        if (!result && (errno == ENOSPC)) {
            if (!rlist_alloc_feasible (rl,
                                       ai->mode,
                                       ai->nnodes,
                                       ai->nslots,
                                       ai->slot_size)) {
                errprintf (errp, "unsatisfiable request");
                errno = EOVERFLOW;
            }
        }
    }
    return result;
}

static int rlist_free_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *rnode = rlist_find_rank (rl, n->rank);
    if (!rnode) {
        errno = ENOENT;
        return -1;
    }
    if (rnode_free_idset (rnode, n->cores->ids) < 0)
        return -1;
    if (rnode->up)
        rl->avail += idset_count (n->cores->ids);
    return 0;
}

static int rlist_alloc_rnode (struct rlist *rl, struct rnode *n)
{
    struct rnode *rnode = rlist_find_rank (rl, n->rank);
    if (!rnode) {
        errno = ENOENT;
        return -1;
    }
    if (rnode_alloc_idset (rnode, n->cores->avail) < 0)
        return -1;
    if (rnode->up)
        rl->avail -= idset_count (n->cores->avail);
    return 0;
}

static int rlist_free_ex (struct rlist *rl,
                          struct rlist *alloc,
                          bool ignore_missing)
{
    zlistx_t *freed = NULL;
    struct rnode *n = NULL;

    if (!(freed = zlistx_new ()))
        return -1;

    n = zlistx_first (alloc->nodes);
    while (n) {
        int rc = rlist_free_rnode (rl, n);

        /* Allow failure to free this rnode if ignore_missing is true.
         * This may have occurred if the resource set shrunk before a
         * job using the removed execution targets completed.
         * Otherwise, add the node to the freed resource set on success.
         */
        if ((rc < 0 && !(ignore_missing && errno == ENOENT))
            || (rc == 0  && !zlistx_add_end (freed, n)))
            goto cleanup;

        n = zlistx_next (alloc->nodes);
    }
    zlistx_destroy (&freed);
    return (0);
cleanup:
    /* re-allocate all freed items */
    n = zlistx_first (freed);
    while (n) {
        rlist_alloc_rnode (rl, n);
        n = zlistx_next (freed);
    }
    zlistx_destroy (&freed);
    return (-1);
}

int rlist_free (struct rlist *rl, struct rlist *alloc)
{
    return rlist_free_ex (rl, alloc, false);
}

int rlist_free_tolerant (struct rlist *rl, struct rlist *alloc)
{
    return rlist_free_ex (rl, alloc, true);
}

int rlist_set_allocated (struct rlist *rl, struct rlist *alloc)
{
    zlistx_t *allocd = NULL;
    struct rnode *n = NULL;
    if (!alloc || !(allocd = zlistx_new ()))
        return -1;
    n = zlistx_first (alloc->nodes);
    while (n) {
        if (rlist_alloc_rnode (rl, n) < 0
            || !zlistx_add_end (allocd, n))
            goto cleanup;
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

size_t rlist_nnodes (const struct rlist *rl)
{
    return zlistx_size (rl->nodes);
}

size_t rlist_count (const struct rlist *rl, const char *type)
{
    struct rnode *n;
    size_t count = 0;
    n = zlistx_first (rl->nodes);
    while (n) {
        count += rnode_count_type (n, type);
        n = zlistx_next (rl->nodes);
    }
    return count;
}

/* Mark all nodes in state 'up'. Count number of cores that changed
 *  availability state.
 */
static int rlist_mark_all (struct rlist *rl, bool up)
{
    int count = 0;
    struct rnode *n = zlistx_first (rl->nodes);
    while (n) {
        if (n->up != up)
            count += idset_count (n->cores->avail);
        n->up = up;
        n = zlistx_next (rl->nodes);
    }
    return count;
}

static int rlist_mark_state (struct rlist *rl, bool up, const char *ids)
{
    int count = 0;
    unsigned int i;
    struct idset *idset = idset_decode (ids);
    if (idset == NULL)
        return -1;
    i = idset_first (idset);
    while (i != IDSET_INVALID_ID) {
        struct rnode *n = rlist_find_rank (rl, i);
        if (n) {
            if (n->up != up)
                count += idset_count (n->cores->avail);
            n->up = up;
        }
        i = idset_next (idset, i);
    }
    idset_destroy (idset);
    return count;
}

int rlist_mark_down (struct rlist *rl, const char *ids)
{
    int count;
    if (streq (ids, "all"))
        count = rlist_mark_all (rl, false);
    else
        count = rlist_mark_state (rl, false, ids);
    rl->avail -= count;
    return 0;
}

int rlist_mark_up (struct rlist *rl, const char *ids)
{
    int count;
    if (streq (ids, "all"))
        count = rlist_mark_all (rl, true);
    else
        count = rlist_mark_state (rl, true, ids);
    rl->avail += count;
    return 0;
}

/*  Check if a resource set provided by configuration is valid.
 *  Returns -1 on failure with error in errp->text.
 */
static int rlist_config_check (struct rlist *rl, flux_error_t *errp)
{
    struct rnode *n;
    struct hostlist *empty;
    int rc = -1;

    if (zlistx_size (rl->nodes) == 0)
        return errprintf (errp, "no hosts configured");

    if (!(empty = hostlist_create ()))
        return errprintf (errp, "hostlist_create: Out of memory");

    n = zlistx_first (rl->nodes);
    while (n) {
        if (rnode_avail_total (n) <= 0) {
            if (hostlist_append (empty, n->hostname) < 0) {
                errprintf (errp,
                           "host %s was assigned no resources",
                           n->hostname);
                goto out;
            }
        }
        n = zlistx_next (rl->nodes);
    }
    if (hostlist_count (empty) > 0) {
        char *s = hostlist_encode (empty);
        errprintf (errp, "resource.config: %s assigned no resources", s);
        free (s);
        goto out;
    }
    rc = 0;
out:
    hostlist_destroy (empty);
    return rc;
}

/*  Process one entry from the resource.config array
 */
static int rlist_config_add_entry (struct rlist *rl,
                                   struct hostlist *hostmap,
                                   flux_error_t *errp,
                                   int index,
                                   const char *hosts,
                                   const char *cores,
                                   const char *gpus,
                                   json_t *properties)
{
    struct hostlist *hl = NULL;
    const char *host = NULL;
    struct idset *coreids = NULL;
    struct idset *gpuids = NULL;
    struct idset *ranks = NULL;
    int rc = -1;

    if (!(hl = hostlist_decode (hosts))) {
        errprintf (errp, "config[%d]: invalid hostlist '%s'", index, hosts);
        goto error;
    }
    if (hostlist_count (hl) == 0) {
        errprintf (errp, "config[%d]: empty hostlist specified", index);
        goto error;
    }
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp, "idset_create: %s", strerror (errno));
        goto error;
    }
    if (cores && !(coreids = idset_decode (cores))) {
        errprintf (errp, "config[%d]: invalid idset cores='%s'", index, cores);
        goto error;
    }
    if (gpus && !(gpuids = idset_decode (gpus))) {
        errprintf (errp, "config[%d]: invalid idset gpus='%s'", index, gpus);
        goto error;
    }
    host = hostlist_first (hl);
    while (host) {
        struct rnode *n;
        int rank = hostlist_find (hostmap, host);
        if (rank < 0) {
            /*
             *  First time encountering this host. Append to host map
             *   hostlist and assign a rank.
             */
            if (hostlist_append (hostmap, host) < 0) {
                errprintf (errp, "failed to append %s to host map", host);
                goto error;
            }
            rank = hostlist_count (hostmap) - 1;
        }
        if (idset_set (ranks, rank) < 0) {
            errprintf (errp, "idset_set(ranks, %d): %s",
                       rank,
                       strerror (errno));
            goto error;
        }
        if (!(n = rnode_new (host, rank))) {
            errprintf (errp, "rnode_new: %s", strerror (errno));
            goto error;
        }
        if (coreids && !rnode_add_child_idset (n, "core", coreids, coreids)) {
            errprintf (errp, "rnode_add_child_idset: %s", strerror (errno));
            goto error;
        }
        if (gpuids && !rnode_add_child_idset (n, "gpu", gpuids, gpuids)) {
            errprintf (errp, "rnode_add_child_idset: %s", strerror (errno));
            goto error;
        }
        if (properties) {
            size_t idx;
            json_t *o;

            json_array_foreach (properties, idx, o) {
                const char *property;
                if (!(property = json_string_value (o))
                    || property_string_invalid (property)) {
                    char *s = json_dumps (o, JSON_ENCODE_ANY);
                    errprintf (errp,
                               "config[%d]: invalid property \"%s\"",
                               index,
                               s);
                    free (s);
                    goto error;
                }
                if (rnode_set_property (n, property) < 0) {
                    errprintf (errp,
                               "Failed to set property %s on rank %u",
                                property,
                                rank);
                    goto error;
                }
            }
        }
        if (rlist_add_rnode (rl, n) < 0) {
            errprintf (errp, "Unable to add rnode: %s", strerror (errno));
            goto error;
        }
        host = hostlist_next (hl);
    }
    rc = 0;
error:
    hostlist_destroy (hl);
    idset_destroy (ranks);
    idset_destroy (coreids);
    idset_destroy (gpuids);
    return rc;
}

struct rlist *rlist_from_config (json_t *conf, flux_error_t *errp)
{
    size_t index;
    json_t *entry;
    struct rlist *rl = NULL;
    struct hostlist *hl = NULL;

    if (!conf || !json_is_array (conf)) {
        errprintf (errp, "resource config must be an array");
        return NULL;
    }

    if (!(hl = hostlist_create ())
        || !(rl = rlist_create ())) {
        errprintf (errp, "Out of memory");
        goto error;
    }

    json_array_foreach (conf, index, entry) {
        const char *hosts = NULL;
        const char *cores = NULL;
        const char *gpus = NULL;
        json_t *properties = NULL;
        json_error_t error;

        if (json_unpack_ex (entry, &error, 0,
                            "{s:s s?s s?s s?o !}",
                            "hosts", &hosts,
                            "cores", &cores,
                            "gpus",  &gpus,
                            "properties", &properties) < 0) {
            errprintf (errp, "config[%zu]: %s", index, error.text);
            goto error;
        }
        if (properties != NULL && !json_is_array (properties)) {
            errprintf (errp,
                       "config[%zu]: %s",
                       index,
                       "properties must be an array");
            goto error;
        }
        if (rlist_config_add_entry (rl,
                                    hl,
                                    errp,
                                    index,
                                    hosts,
                                    cores,
                                    gpus,
                                    properties) < 0)
            goto error;
    }

    if (rlist_config_check (rl, errp) < 0)
        goto error;

    hostlist_destroy (hl);
    return rl;
error:
    hostlist_destroy (hl);
    rlist_destroy (rl);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
