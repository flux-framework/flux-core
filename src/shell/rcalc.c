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

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <jansson.h>
#include <flux/taskmap.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "rcalc.h"

struct rankinfo {
    int id;
    int rank;
    int ncores;
    int ngpus;
    const char *cores;
    const char *gpus;
    struct idset *cpuset;
    struct idset *gpuset;
};

struct allocinfo {
    int ncores_avail;
    int ntasks;
};

struct rcalc {
    json_t *json;
    json_t *R_lite;
    struct idset *orig_ranks;
    int nranks;
    int ncores;
    int ngpus;
    int ntasks;
    struct rankinfo *ranks;
    struct allocinfo *alloc;
};

int rcalc_update_map (rcalc_t *r, struct taskmap *map)
{
    if (r == NULL || map == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* Update task counts for each rank in rcalc based on new taskmap.
     * N.B.: ignores alloc[i]->ncores_avail since this variable is known
     * to not be used after the initial distribution of tasks.
     */
    for (int i = 0; i < r->nranks; i++)
        r->alloc[i].ntasks = taskmap_ntasks (map, i);

    return 0;
}

void rcalc_destroy (rcalc_t *r)
{
    if (r == NULL)
        return;
    json_decref (r->json);
    idset_destroy (r->orig_ranks);
    for (int i = 0; i < r->nranks; i++) {
        idset_destroy (r->ranks[i].cpuset);
        idset_destroy (r->ranks[i].gpuset);
    }
    free (r->ranks);
    free (r->alloc);
    memset (r, 0, sizeof (*r));
    free (r);
}

static struct idset * rcalc_ranks (rcalc_t *r, flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    json_error_t error;
    struct idset *ranks;

    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;

    json_array_foreach (r->R_lite, index, entry) {
        struct idset *ids;
        const char *rank;
        if (json_unpack_ex (entry, &error, 0,
                            "{s:s}",
                            "rank", &rank) < 0) {
            errprintf (errp, "%s", error.text);
            goto err;
        }
        if (!(ids = idset_decode (rank))) {
            errprintf (errp, "invalid idset %s", rank);
            goto err;
        }
        if (idset_add (ranks, ids) < 0) {
            idset_destroy (ids);
            errprintf (errp, "idset_add (%s): %s", rank, strerror (errno));
            goto err;
        }
        idset_destroy (ids);
    }
    return ranks;
err:
    idset_destroy (ranks);
    return NULL;
}

static int rankinfo_get_children (struct rankinfo *ri,
                                  json_t *children,
                                  flux_error_t *errp)
{
    json_error_t error;

    if (json_unpack_ex (children, &error, 0,
                        "{s:s s?s}",
                        "core", &ri->cores,
                        "gpu", &ri->gpus) < 0)
        return errprintf (errp, "%s", error.text);

    if (!(ri->cpuset = idset_decode (ri->cores))
        || !(ri->gpuset = idset_decode (ri->gpus ? ri->gpus : "")))
        return errprintf (errp, "Failed to decode cpu or gpu sets");

    ri->ncores = idset_count (ri->cpuset);
    ri->ngpus = idset_count (ri->gpuset);

    return 0;
}

static int cmp_rank (const void *a, const void *b)
{
    const struct rankinfo *ra = a;
    const struct rankinfo *rb = b;
    return (ra->rank < rb->rank ? -1 : 1);
}


static int rcalc_process_all_ranks (rcalc_t *r, flux_error_t *errp)
{
    json_t *entry;
    size_t index;
    json_error_t error;
    int n = 0;
    unsigned int max_rank = 0;
    bool sorted = true;

    json_array_foreach (r->R_lite, index, entry) {
        const char *rank;
        json_t *children;
        unsigned int i;
        struct idset *ids;

        if (json_unpack_ex (entry, &error, 0,
                            "{s:s s:o}",
                            "rank", &rank,
                            "children", &children) < 0)
            return errprintf (errp, "%s", error.text);

        if (!(ids = idset_decode (rank)))
            return errprintf (errp,
                              "idset_decode (%s): %s",
                              rank,
                              strerror (errno));

        i = idset_first (ids);
        while (i != IDSET_INVALID_ID) {
            struct rankinfo *ri = &r->ranks[n];
            ri->id = n;
            ri->rank = i;

            /* Detect if R_lite ranks are unordered:
             */
            if (i >= max_rank)
                max_rank = i;
            else
                sorted = false;

            if (rankinfo_get_children (ri, children, errp) < 0) {
                idset_destroy (ids);
                return -1;
            }
            r->ncores += ri->ncores;
            r->ngpus += ri->ngpus;
            n++;
            i = idset_next (ids, i);
        }
        idset_destroy (ids);
    }

    if (!sorted) {
        /* Need to sort r->ranks by broker rank and reassign local rank (id):
         */
        qsort (r->ranks, r->nranks, sizeof (struct rankinfo), cmp_rank);
        for (int id = 0; id < r->nranks; id++)
            r->ranks[id].id = id;
    }

    return 0;
}

rcalc_t * rcalc_create_json (json_t *o)
{
    int version;
    flux_error_t error;
    rcalc_t *r = calloc (1, sizeof (*r));
    if (!r)
        return (NULL);
    r->json = json_incref (o);
    if (json_unpack_ex (o, NULL, 0,
                        "{s:i s:{s:o}}",
                        "version", &version,
                        "execution",
                        "R_lite", &r->R_lite) < 0)
        goto fail;
    if (version != 1) {
        errno = EINVAL;
        goto fail;
    }
    if (!(r->orig_ranks = rcalc_ranks (r, &error)))
        goto fail;
    r->nranks = idset_count (r->orig_ranks);
    r->ranks = calloc (r->nranks, sizeof (struct rankinfo));
    r->alloc = calloc (r->nranks, sizeof (struct allocinfo));

    if (rcalc_process_all_ranks (r, &error) < 0)
        goto fail;

    return (r);
fail:
    rcalc_destroy (r);
    return (NULL);
}

rcalc_t *rcalc_create (const char *json_in)
{
    rcalc_t *r = NULL;
    json_t *o = NULL;

    if (!(o = json_loads (json_in, 0, 0))) {
        errno = EINVAL;
        return (NULL);
    }
    r = rcalc_create_json (o);
    json_decref (o);
    return (r);
}

rcalc_t *rcalc_createf (FILE *fp)
{
    rcalc_t *r;
    json_t *o;
    if (!(o = json_loadf (fp, 0, 0))) {
        errno = EINVAL;
        return (NULL);
    }
    r = rcalc_create_json (o);
    json_decref (o);
    return (r);
}

int rcalc_total_cores (rcalc_t *r)
{
    return r->ncores;
}

int rcalc_total_gpus (rcalc_t *r)
{
    return r->ngpus;
}

int rcalc_total_ntasks (rcalc_t *r)
{
    return r->ntasks;
}

int rcalc_total_nodes_used (rcalc_t *r)
{
    int i;
    int count = 0;
    for (i = 0; i < r->nranks; i++)
        if (r->alloc[i].ntasks > 0)
            count++;
    return (count);
}

int rcalc_total_nodes (rcalc_t *r)
{
    return r->nranks;
}

static void allocinfo_reset_avail (rcalc_t *r)
{
    for (int i = 0; i < r->nranks; i++)
        r->alloc[i].ncores_avail = r->ranks[i].ncores;
}

static void allocinfo_clear (rcalc_t *r)
{
    memset (r->alloc, 0, sizeof (struct allocinfo) * r->nranks);
    allocinfo_reset_avail (r);
}

static int cmp_alloc_cores (struct allocinfo *x, struct allocinfo *y)
{
    return (x->ncores_avail < y->ncores_avail);
}

zlist_t *alloc_list_sorted (rcalc_t *r)
{
    int i;
    zlist_t *l = zlist_new ();
    if (l == NULL)
        return (NULL);
    for (i = 0; i < r->nranks; i++)
        zlist_append (l, &r->alloc[i]);
    zlist_sort (l, (zlist_compare_fn *) cmp_alloc_cores);
    return (l);
}

static bool allocinfo_add_task (struct allocinfo *ai, int size)
{
    if (ai->ncores_avail >= size) {
        ai->ntasks++;
        ai->ncores_avail -= size;
        return (true);
    }
    return (false);
}

/*
 *  Distribute ntasks over the ranks in `r` "evenly" by a heuristic
 *   that first assigns a number of cores per task, then distributes
 *   over largest nodes first.
 */
int rcalc_distribute (rcalc_t *r, int ntasks, int cores_per_task)
{
    struct allocinfo *ai;
    int assigned = 0;
    zlist_t *l = NULL;

    if (cores_per_task <= 0) {
        /* Punt for now if there are more tasks than cores */
        if ((cores_per_task = r->ncores/ntasks) == 0) {
            errno = EINVAL;
            return -1;
        }
    }

    r->ntasks = ntasks;
    /* Reset the allocation info array and get a sorted list of
     * ranks by "largest" first
     */
    allocinfo_clear (r);
    if (!(l = alloc_list_sorted (r)))
        return (-1);

    /* Does the smallest node have enough room to fit a task? */
    ai = zlist_last (l);
    if (ai->ncores_avail < cores_per_task)
        cores_per_task = ai->ncores_avail;

    /* Assign tasks to largest ranks first, pushing "used" to the back
     *  and leaving "full" ranks off the list.
     */
    while (assigned < ntasks) {
        if (!(ai = zlist_pop (l))) {
            /* Uh oh, we ran out of cores. Allow oversubscription by refilling
             * the allocinfo array and continue
             */
            zlist_destroy (&l);
            allocinfo_reset_avail (r);
            if (!(l = alloc_list_sorted (r)))
                return (-1);
            continue;
        }
        if (allocinfo_add_task (ai, cores_per_task)) {
            zlist_append (l, ai);
            assigned++;
        }
    }
    zlist_destroy (&l);
    return (0);
}

/*  Distribute tasks over resources in `r` by resource type. Assigns
 *   ntasks tasks to each resource of type name.
 */
int rcalc_distribute_per_resource (rcalc_t *r, const char *name, int ntasks)
{
    bool by_core = false;
    bool by_node = false;

    if (streq (name, "core"))
        by_core = true;
    else if (streq (name, "node"))
        by_node = true;
    else {
        errno = EINVAL;
        return  -1;
    }

    allocinfo_clear (r);
    r->ntasks = 0;
    for (int i = 0; i < r->nranks; i++) {
        if (by_node) {
            r->alloc[i].ntasks = ntasks;
            r->alloc[i].ncores_avail = 0;
            r->ntasks += ntasks;
        }
        else if (by_core) {
            int n = r->alloc[i].ncores_avail * ntasks;
            r->alloc[i].ntasks = n;
            r->alloc[i].ncores_avail = 0;
            r->ntasks += n;
        }
    }
    return 0;
}

static struct rankinfo *rcalc_rankinfo_find (rcalc_t *r, int rank)
{
    int i;
    for (i = 0; i < r->nranks; i++) {
        struct rankinfo *ri = &r->ranks[i];
        if (ri->rank == rank)
            return (ri);
    }
    return (NULL);
}

static void strcpy_trunc (char *dst, size_t dstlen, const char *src)
{
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    if (strlen (src) < dstlen)
        strcpy (dst, src);
    else {
        strncpy (dst, src, dstlen-1);
        dst[dstlen-2] = '+'; /* Indicate truncation */
    }
}

static void rcalc_rankinfo_set (rcalc_t *r, int id,
                                struct rcalc_rankinfo *rli)
{
    struct rankinfo *ri = &r->ranks[id];
    struct allocinfo *ai = &r->alloc[id];
    rli->nodeid = ri->id;
    rli->rank =   ri->rank;
    rli->ncores = ri->ncores;
    rli->ntasks = ai->ntasks;
    /*  Copy cores string to rli, in the very unlikely event that
     *   we get a huge cores string, indicate truncation.
     */
    strcpy_trunc (rli->cores, sizeof (rli->cores), ri->cores);
    strcpy_trunc (rli->gpus, sizeof (rli->gpus), ri->gpus);
}

int rcalc_get_rankinfo (rcalc_t *r, int rank, struct rcalc_rankinfo *rli)
{
    struct rankinfo *ri = rcalc_rankinfo_find (r, rank);
    if (ri == NULL) {
        errno = ENOENT;
        return (-1);
    }
    rcalc_rankinfo_set (r, ri->id, rli);
    return (0);
}

int rcalc_get_nth (rcalc_t *r, int n, struct rcalc_rankinfo *rli)
{
    if (n >= r->nranks) {
        errno = EINVAL;
        return (-1);
    }
    rcalc_rankinfo_set (r, n, rli);
    return (0);
}

int rcalc_has_rank (rcalc_t *r, int rank)
{
    if (rcalc_rankinfo_find (r, rank))
        return (1);
    return (0);
}

/*
 * vi: ts=4 sw=4 expandtab
 */
