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
#include <czmq.h>   /* zlist_t */

#include "src/common/libidset/idset.h"

#include "rcalc.h"

struct rankinfo {
    int id;
    int rank;
    int ncores;
    const char *cores;
    cpu_set_t cpuset;
};

struct allocinfo {
    int ncores_avail;
    int ntasks;
    int basis;
};

struct rcalc {
    json_t *json;
    int nranks;
    int ncores;
    int ntasks;
    struct rankinfo *ranks;
    struct allocinfo *alloc;
};


static const char * nexttoken (const char *p, int sep)
{
    if (p)
        p = strchr (p, sep);
    if (p)
        p++;
    return (p);
}

/*
 *  Temporarily copied from src/bindings/lua/lua-affinity
 */
static int cstr_to_cpuset(cpu_set_t *mask, const char* str)
{
    const char *p, *q;
    char *endptr;
    q = str;
    CPU_ZERO(mask);

    if (strlen (str) == 0)
        return 0;

    while (p = q, q = nexttoken(q, ','), p) {
        unsigned long a; /* beginning of range */
        unsigned long b; /* end of range */
        unsigned long s; /* stride */
        const char *c1, *c2;

        a = strtoul(p, &endptr, 10);
        if (endptr == p)
            return EINVAL;
        if (a >= CPU_SETSIZE)
            return E2BIG;
        /*
         *  Leading zeros are an error:
         */
        if ((a != 0 && *p == '0') || (a == 0 && memcmp (p, "00", 2L) == 0))
            return 1;

        b = a;
        s = 1;

        c1 = nexttoken(p, '-');
        c2 = nexttoken(p, ',');
        if (c1 != NULL && (c2 == NULL || c1 < c2)) {

            /*
             *  Previous conversion should have used up all characters
             *     up to next '-'
             */
            if (endptr != (c1-1)) {
                return 1;
            }

            b = strtoul (c1, &endptr, 10);
            if (endptr == c1)
                return EINVAL;
            if (b >= CPU_SETSIZE)
                return E2BIG;

            c1 = nexttoken(c1, ':');
            if (c1 != NULL && (c2 == NULL || c1 < c2)) {
                s = strtoul (c1, &endptr, 10);
                if (endptr == c1)
                    return EINVAL;
                if (b >= CPU_SETSIZE)
                    return E2BIG;
            }
        }

        if (!(a <= b))
            return EINVAL;
        while (a <= b) {
            CPU_SET(a, mask);
            a += s;
        }
    }

    /*  Error if there are left over characters */
    if (endptr && *endptr != '\0')
        return EINVAL;

    return 0;
}


static int rankinfo_get (json_t *o, struct rankinfo *ri)
{
    json_error_t error;
    int rc = json_unpack_ex (o, &error, 0, "{s:i, s:{s:s}}",
                "rank", &ri->rank,
                "children",
                "core", &ri->cores);
    if (rc < 0) {
        fprintf (stderr, "json_unpack: %s\n", error.text);
        return -1;
    }

    if (!ri->cores || cstr_to_cpuset (&ri->cpuset, ri->cores))
        return -1;

    ri->ncores = CPU_COUNT (&ri->cpuset);
    return (0);
}

static int expand_rank_ranges_one (json_t *out, json_t *entry)
{
    const char *rank;
    json_t *children;
    struct idset *ids;
    unsigned int id;
    json_t *n;

    if (json_unpack_ex (entry, NULL, 0,
                        "{s:s s:o}",
                        "rank", &rank,
                        "children", &children) < 0)
        return -1;
    if (!(ids = idset_decode (rank)))
        return -1;
    id = idset_first (ids);
    while (id != IDSET_INVALID_ID) {
        if (!(n = json_pack ("{s:i s:O}",
                             "rank", id,
                             "children", children)))
            return -1;
        if (json_array_append_new (out, n) < 0) {
            json_decref (n);
            return -1;
        }
        id = idset_next (ids, id);
    }
    idset_destroy (ids);
    return 0;
}

/* Convert from R version 1 internal R_lite object to the earlier wreck R_lite
 * object understood by this module.  They are the same except Rv1 specifies
 * ranks as an idset rather than integer, allowing for compact representation.
 */
static json_t *expand_rank_ranges (json_t *R_lite)
{
    json_t *out;
    json_t *entry;
    size_t index;

    if (!(out = json_array ())) // accumulate new array
        return NULL;
    json_array_foreach (R_lite, index, entry) {
        if (expand_rank_ranges_one (out, entry) < 0)
            goto error;
    }
    return out;
error:
    json_decref (out);
    return NULL;
}

void rcalc_destroy (rcalc_t *r)
{
    json_decref (r->json);
    free (r->ranks);
    free (r->alloc);
    memset (r, 0, sizeof (*r));
    free (r);
}

static rcalc_t * rcalc_create_json (json_t *o)
{
    int i;
    int version;
    json_t *R_lite;
    rcalc_t *r = calloc (1, sizeof (*r));
    if (!r)
        return (NULL);
    if (json_unpack_ex (o, NULL, 0,
                        "{s:i s:{s:o}}",
                        "version", &version,
                        "execution",
                        "R_lite", &R_lite) < 0)
        goto fail;
    if (version != 1) {
        errno = EINVAL;
        goto fail;
    }
    if (!(r->json = expand_rank_ranges (R_lite))) {
        errno = EINVAL;
        goto fail;
    }
    r->nranks = json_array_size (r->json);
    r->ranks = calloc (r->nranks, sizeof (struct rankinfo));
    r->alloc = calloc (r->nranks, sizeof (struct allocinfo));
    for (i = 0; i < r->nranks; i++) {
        r->ranks[i].id = i;
        if (rankinfo_get (json_array_get (r->json, i), &r->ranks[i]) < 0)
            goto fail;
        r->ncores += r->ranks[i].ncores;
    }
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


static void allocinfo_clear (rcalc_t *r)
{
    int i;
    memset (r->alloc, 0, sizeof (struct allocinfo) * r->nranks);
    for (i = 0; i < r->nranks; i++)
        r->alloc[i].ncores_avail = r->ranks[i].ncores;
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

static void rcalc_compute_taskids (rcalc_t *r)
{
    int i;
    int taskid = 0;
    for (i = 0; i < r->nranks; i++) {
        r->alloc[i].basis = taskid;
        taskid += r->alloc[i].ntasks;
    }
}

/*
 *  Distribute ntasks over the ranks in `r` "evenly" by a heuristic
 *   that first assigns a number of cores per task, then distributes
 *   over largest nodes first.
 */
int rcalc_distribute (rcalc_t *r, int ntasks)
{
    struct allocinfo *ai;
    int assigned = 0;
    int cores_per_task = 0;
    zlist_t *l = NULL;

    /* Punt for now if there are more tasks than cores */
    if ((cores_per_task = r->ncores/ntasks) == 0) {
        errno = EINVAL;
        return -1;
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
        ai = zlist_pop (l);
        if (allocinfo_add_task (ai, cores_per_task)) {
            zlist_append (l, ai);
            assigned++;
        }
    }
    zlist_destroy (&l);

    /*  Assign taskid basis to each rank in block allocation order */
    rcalc_compute_taskids (r);
    return (0);
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

static void rcalc_rankinfo_set (rcalc_t *r, int id,
                                struct rcalc_rankinfo *rli)
{
    int coreslen = sizeof (rli->cores);
    struct rankinfo *ri = &r->ranks[id];
    struct allocinfo *ai = &r->alloc[id];
    rli->nodeid = ri->id;
    rli->rank =   ri->rank;
    rli->ncores = ri->ncores;
    rli->ntasks = ai->ntasks;
    rli->global_basis =  ai->basis;
    memcpy (&rli->cpuset, &ri->cpuset, sizeof (cpu_set_t));
    /*  Copy cores string to rli, in the very unlikely event that
     *   we get a huge cores string, indicate truncation.
     */
    if (strlen (ri->cores) < coreslen)
        strcpy (rli->cores, ri->cores);
    else {
        strncpy (rli->cores, ri->cores, coreslen-1);
        rli->cores [coreslen-2] = '+'; /* Indicate truncation */
    }
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
