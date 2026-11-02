/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* slotmap - map job tasks to RFC 20 task slots
 *
 * Each shell holds a slotmap for the whole job, so its size must scale
 * with the number of ranks, not the number of slots or tasks. Therefore,
 * slots are not stored individually. Instead, each rank has a short list
 * of slot groups, where a group is `count` slots with the same label that
 * divide the group's resources by a fixed rule (see struct group). The
 * resources of a single slot are computed only when needed.
 *
 * The slots of a rank are numbered in group order. Tasks are assigned to
 * the "active" slots of a rank: the slots selected by the label passed to
 * slotmap_distribute(), or all slots of the rank if none were selected.
 * The tasks of a rank are dealt to its active slots round-robin, so the
 * task count of each slot is derived from the task count of the rank.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <jansson.h>
#include <flux/taskmap.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

#include "slotmap.h"

/* How the resources of a group are divided among its slots:
 * - SPLIT_EVEN: divide evenly, giving any remainder to the first slots.
 * - SPLIT_FIXED: each slot gets `core_size` cores and up to `gpu_size`
 *   gpus. Once the cores are used up, later slots reuse them from the
 *   start (oversubscription).
 */
enum split {
    SPLIT_EVEN,
    SPLIT_FIXED,
};

struct group {
    const char *label;          /* interned in slotmap labels */
    int count;
    bool active;
    enum split split;
    int core_size;              /* SPLIT_FIXED only */
    int gpu_size;               /* SPLIT_FIXED only */
    struct idset *cores;
    struct idset *gpus;
};

struct rankinfo {
    int nodeid;
    int rank;
    int ntasks;
    struct idset *cores;
    struct idset *gpus;
    struct group *groups;
    int ngroups;
};

struct slotmap {
    int nranks;
    int ncores;
    int ngpus;
    int ntasks;
    int nslots_R;
    bool has_slots;
    json_t *labels;
    struct rankinfo *ranks;
};

/* A parsed R_lite entry
 */
struct entry {
    const char *ranks;
    json_t *children;
    json_t *slots;
    struct idset *ids;
    struct idset *cores;
    struct idset *gpus;
};

static void group_clear (struct group *g)
{
    idset_destroy (g->cores);
    idset_destroy (g->gpus);
}

static void rank_clear_groups (struct rankinfo *ri)
{
    for (int i = 0; i < ri->ngroups; i++)
        group_clear (&ri->groups[i]);
    free (ri->groups);
    ri->groups = NULL;
    ri->ngroups = 0;
}

static void slots_clear (struct slotmap *sm)
{
    for (int i = 0; i < sm->nranks; i++)
        rank_clear_groups (&sm->ranks[i]);
    sm->has_slots = false;
}

void slotmap_destroy (struct slotmap *sm)
{
    if (sm) {
        int saved_errno = errno;
        if (sm->ranks) {
            slots_clear (sm);
            for (int i = 0; i < sm->nranks; i++) {
                idset_destroy (sm->ranks[i].cores);
                idset_destroy (sm->ranks[i].gpus);
            }
            free (sm->ranks);
        }
        json_decref (sm->labels);
        free (sm);
        errno = saved_errno;
    }
}

/* Return a copy of `label` owned by the slotmap.
 */
static const char *label_intern (struct slotmap *sm, const char *label)
{
    json_t *o;

    if (!label)
        label = "task";
    if (!(o = json_object_get (sm->labels, label))) {
        if (!(o = json_string (label))
            || json_object_set_new (sm->labels, label, o) < 0) {
            errno = ENOMEM;
            return NULL;
        }
    }
    return json_string_value (o);
}

/* Append a group of `count` slots to `ri` with copies of `cores` and
 * `gpus`. Return the new group, or NULL on failure.
 */
static struct group *group_append (struct slotmap *sm,
                                   struct rankinfo *ri,
                                   const char *label,
                                   int count,
                                   enum split split,
                                   const struct idset *cores,
                                   const struct idset *gpus)
{
    struct group *groups;
    struct group *g;
    size_t size = (ri->ngroups + 1) * sizeof (*groups);

    if (!(groups = realloc (ri->groups, size)))
        return NULL;
    ri->groups = groups;
    g = &groups[ri->ngroups];
    memset (g, 0, sizeof (*g));
    g->count = count;
    g->split = split;
    if (!(g->label = label_intern (sm, label))
        || !(g->cores = idset_copy (cores))
        || !(g->gpus = idset_copy (gpus))) {
        group_clear (g);
        return NULL;
    }
    ri->ngroups++;
    return g;
}

/* Return a new idset with the ids of `ids` at positions [first, first+n).
 */
static struct idset *idset_slice (const struct idset *ids, int first, int n)
{
    struct idset *result;
    unsigned int id;
    int i = 0;

    if (!(result = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    id = idset_first (ids);
    while (id != IDSET_INVALID_ID && i < first + n) {
        if (i >= first && idset_set (result, id) < 0) {
            idset_destroy (result);
            return NULL;
        }
        id = idset_next (ids, id);
        i++;
    }
    return result;
}

/* Return the number of `total` ids given to each of the first `m` of
 * `count` slots under SPLIT_EVEN.
 */
static int even_prefix (int total, int count, int m)
{
    return m * (total / count) + (m < total % count ? m : total % count);
}

/* Get the position and number of the cores and gpus of slot `i` of `g`.
 */
static void group_slot_range (struct group *g,
                              int i,
                              int *core_first,
                              int *ncores,
                              int *gpu_first,
                              int *ngpus)
{
    int total_cores = idset_count (g->cores);
    int total_gpus = idset_count (g->gpus);

    if (g->split == SPLIT_EVEN) {
        *core_first = even_prefix (total_cores, g->count, i);
        *ncores = even_prefix (total_cores, g->count, i + 1) - *core_first;
        *gpu_first = even_prefix (total_gpus, g->count, i);
        *ngpus = even_prefix (total_gpus, g->count, i + 1) - *gpu_first;
    }
    else {
        int fit = g->core_size > 0 ? total_cores / g->core_size : 0;
        int j = fit > 0 ? i % fit : i;
        *core_first = j * g->core_size;
        *ncores = g->core_size;
        *gpu_first = j * g->gpu_size;
        *ngpus = g->gpu_size;
    }
}

/* Return the cores and gpus of slot `i` of group `g` in new idsets.
 */
static int group_slot (struct group *g,
                       int i,
                       struct idset **cores,
                       struct idset **gpus)
{
    int core_first, ncores, gpu_first, ngpus;

    group_slot_range (g, i, &core_first, &ncores, &gpu_first, &ngpus);
    if (!(*cores = idset_slice (g->cores, core_first, ncores)))
        return -1;
    if (!(*gpus = idset_slice (g->gpus, gpu_first, ngpus))) {
        idset_destroy (*cores);
        return -1;
    }
    return 0;
}

/* Return the number of active slots of `ri`. If no groups are active,
 * all slots are active.
 */
static int rank_nactive (struct rankinfo *ri, bool *all)
{
    int n = 0;
    for (int i = 0; i < ri->ngroups; i++)
        if (ri->groups[i].active)
            n += ri->groups[i].count;
    *all = (n == 0);
    if (n == 0) {
        for (int i = 0; i < ri->ngroups; i++)
            n += ri->groups[i].count;
    }
    return n;
}

/* Return the number of tasks assigned to active slot `p` of `ri`, which
 * has `nactive` active slots.
 */
static int active_slot_ntasks (struct rankinfo *ri, int nactive, int p)
{
    return ri->ntasks / nactive + (p < ri->ntasks % nactive ? 1 : 0);
}

static int cmp_rank (const void *a, const void *b)
{
    const struct rankinfo *ra = a;
    const struct rankinfo *rb = b;
    return ra->rank - rb->rank;
}

static struct rankinfo *rankinfo_find (struct slotmap *sm, int rank)
{
    struct rankinfo key = { .rank = rank };
    return bsearch (&key, sm->ranks, sm->nranks, sizeof (key), cmp_rank);
}

/* Check that the ids of `type` in `slots` exactly cover the ids of
 * `type` in children, given by `val`, with no overlap between slots.
 */
static int slots_check_type (json_t *slots,
                             const char *type,
                             json_t *val,
                             const char *ranks,
                             flux_error_t *errp)
{
    const char *label;
    json_t *entry;
    struct idset *all = NULL;
    struct idset *used = NULL;
    struct idset *ids = NULL;
    struct idset *diff = NULL;
    int rc = -1;

    if (!json_is_string (val)
        || !(all = idset_decode (json_string_value (val)))
        || !(used = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp,
                   "rank %s: invalid children.%s",
                   ranks,
                   type);
        goto out;
    }
    json_object_foreach (slots, label, entry) {
        json_t *o = json_object_get (entry, type);
        if (!o)
            continue;
        if (!json_is_string (o)
            || !(ids = idset_decode (json_string_value (o)))) {
            errprintf (errp,
                       "rank %s: invalid slots.%s.%s",
                       ranks,
                       label,
                       type);
            goto out;
        }
        if (!(diff = idset_difference (ids, all))) {
            errprintf (errp, "out of memory");
            goto out;
        }
        if (!idset_empty (diff)) {
            errprintf (errp,
                       "rank %s: slots.%s.%s not in children.%s",
                       ranks,
                       label,
                       type,
                       type);
            goto out;
        }
        if (idset_has_intersection (used, ids)) {
            errprintf (errp,
                       "rank %s: slots.%s.%s overlaps another slot",
                       ranks,
                       label,
                       type);
            goto out;
        }
        if (idset_add (used, ids) < 0) {
            errprintf (errp, "out of memory");
            goto out;
        }
        idset_destroy (ids);
        idset_destroy (diff);
        ids = diff = NULL;
    }
    if (!idset_equal (all, used)) {
        errprintf (errp,
                   "rank %s: children.%s not fully assigned to slots",
                   ranks,
                   type);
        goto out;
    }
    rc = 0;
out:
    idset_destroy (all);
    idset_destroy (used);
    idset_destroy (ids);
    idset_destroy (diff);
    return rc;
}

/* Return the number of cores in slots entry `val`, which has been
 * validated by slots_check_type().
 */
static int slots_ncores (json_t *val)
{
    const char *s = json_string_value (json_object_get (val, "core"));
    struct idset *ids;
    int n = 0;

    if (s && (ids = idset_decode (s))) {
        n = idset_count (ids);
        idset_destroy (ids);
    }
    return n;
}

/* Validate the slots of entry `e` against its children and return the
 * number of slots per rank. Every slot must have at least one core.
 */
static int slots_validate (struct entry *e, flux_error_t *errp)
{
    const char *key;
    json_t *val;
    int total = 0;

    if (!json_is_object (e->slots) || json_object_size (e->slots) == 0)
        return errprintf (errp,
                          "rank %s: slots must be a non-empty object",
                          e->ranks);
    json_object_foreach (e->children, key, val) {
        if (slots_check_type (e->slots, key, val, e->ranks, errp) < 0)
            return -1;
    }
    json_object_foreach (e->slots, key, val) {
        const char *label = key;
        const char *type;
        json_t *o;
        json_int_t count;

        if (!json_is_object (val)
            || !(o = json_object_get (val, "count"))
            || !json_is_integer (o)
            || (count = json_integer_value (o)) <= 0
            || count > INT_MAX - total)
            return errprintf (errp,
                              "rank %s: slots.%s.count must be an integer > 0",
                              e->ranks,
                              label);
        json_object_foreach (val, type, o) {
            if (!streq (type, "count")
                && !json_object_get (e->children, type))
                return errprintf (errp,
                                  "rank %s: slots.%s.%s not in children",
                                  e->ranks,
                                  label,
                                  type);
        }
        if (count > slots_ncores (val))
            return errprintf (errp,
                              "rank %s: slots.%s.count exceeds its cores",
                              e->ranks,
                              label);
        total += count;
    }
    return total;
}

static void entry_clear (struct entry *e)
{
    idset_destroy (e->ids);
    idset_destroy (e->cores);
    idset_destroy (e->gpus);
}

static int entry_parse (json_t *o, struct entry *e, flux_error_t *errp)
{
    json_error_t error;
    const char *core = NULL;
    const char *gpu = NULL;

    if (json_unpack_ex (o,
                        &error,
                        0,
                        "{s:s s:o s?o}",
                        "rank", &e->ranks,
                        "children", &e->children,
                        "slots", &e->slots) < 0)
        return errprintf (errp, "%s", error.text);
    if (json_unpack_ex (e->children,
                        &error,
                        0,
                        "{s:s s?s}",
                        "core", &core,
                        "gpu", &gpu) < 0)
        return errprintf (errp,
                          "rank %s: %s",
                          e->ranks,
                          error.text);
    if (!(e->ids = idset_decode (e->ranks)))
        return errprintf (errp,
                          "invalid rank idset %s",
                          e->ranks);
    if (!(e->cores = idset_decode (core))
        || !(e->gpus = idset_decode (gpu ? gpu : "")))
        return errprintf (errp,
                          "rank %s: invalid core or gpu idset",
                          e->ranks);
    return 0;
}

/* Parse and validate all R_lite entries into `entries`. Return the total
 * number of slots in R, or 0 if R has no slots.
 */
static int entries_parse (json_t *R_lite,
                          struct entry *entries,
                          flux_error_t *errp)
{
    struct idset *all;
    json_t *o;
    size_t index;
    int64_t nslots = 0;
    int nentries_with_slots = 0;
    int rc = -1;

    if (!(all = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return errprintf (errp, "out of memory");
    json_array_foreach (R_lite, index, o) {
        struct entry *e = &entries[index];
        int count;

        if (entry_parse (o, e, errp) < 0)
            goto out;
        if (idset_has_intersection (all, e->ids)) {
            errprintf (errp,
                       "rank %s appears in multiple R_lite entries",
                       e->ranks);
            goto out;
        }
        if (idset_add (all, e->ids) < 0) {
            errprintf (errp, "out of memory");
            goto out;
        }
        if (e->slots) {
            if ((count = slots_validate (e, errp)) < 0)
                goto out;
            nslots += (int64_t) count * idset_count (e->ids);
            if (nslots > INT_MAX) {
                errprintf (errp, "too many slots in R");
                goto out;
            }
            nentries_with_slots++;
        }
    }
    if (nentries_with_slots > 0
        && nentries_with_slots != json_array_size (R_lite)) {
        errprintf (errp, "slots must be present in all R_lite entries or none");
        goto out;
    }
    rc = nslots;
out:
    idset_destroy (all);
    return rc;
}

/* Create the rankinfo array from parsed entries, sorted by rank so that
 * nodeid is the array index.
 */
static int ranks_create (struct slotmap *sm,
                         struct entry *entries,
                         int nentries,
                         flux_error_t *errp)
{
    int nranks = 0;
    int n = 0;

    for (int i = 0; i < nentries; i++)
        nranks += idset_count (entries[i].ids);
    if (!(sm->ranks = calloc (nranks + 1, sizeof (struct rankinfo))))
        return errprintf (errp, "out of memory");
    sm->nranks = nranks;
    for (int i = 0; i < nentries; i++) {
        struct entry *e = &entries[i];
        unsigned int id;

        for (id = idset_first (e->ids);
             id != IDSET_INVALID_ID;
             id = idset_next (e->ids, id)) {
            struct rankinfo *ri = &sm->ranks[n++];
            ri->rank = id;
            if (!(ri->cores = idset_copy (e->cores))
                || !(ri->gpus = idset_copy (e->gpus)))
                return errprintf (errp, "out of memory");
            sm->ncores += idset_count (ri->cores);
            sm->ngpus += idset_count (ri->gpus);
        }
    }
    qsort (sm->ranks, sm->nranks, sizeof (struct rankinfo), cmp_rank);
    for (int i = 0; i < sm->nranks; i++)
        sm->ranks[i].nodeid = i;
    return 0;
}

static int cmp_group (const void *a, const void *b)
{
    const struct group *x = a;
    const struct group *y = b;
    unsigned int xid = idset_first (x->cores);
    unsigned int yid = idset_first (y->cores);

    return xid < yid ? -1 : xid > yid ? 1 : 0;
}

/* Create one group per label for each rank of entry `e`.
 */
static int groups_create_from_entry (struct slotmap *sm, struct entry *e)
{
    const char *label;
    json_t *val;

    json_object_foreach (e->slots, label, val) {
        int count = json_integer_value (json_object_get (val, "count"));
        const char *core = json_string_value (json_object_get (val, "core"));
        const char *gpu = json_string_value (json_object_get (val, "gpu"));
        struct idset *cores = idset_decode (core ? core : "");
        struct idset *gpus = idset_decode (gpu ? gpu : "");
        unsigned int id = idset_first (e->ids);
        int rc = (cores && gpus) ? 0 : -1;

        while (rc == 0 && id != IDSET_INVALID_ID) {
            if (!group_append (sm,
                               rankinfo_find (sm, id),
                               label,
                               count,
                               SPLIT_EVEN,
                               cores,
                               gpus))
                rc = -1;
            id = idset_next (e->ids, id);
        }
        idset_destroy (cores);
        idset_destroy (gpus);
        if (rc < 0)
            return -1;
    }
    return 0;
}

static int slotmap_load (struct slotmap *sm, json_t *R, flux_error_t *errp)
{
    json_error_t error;
    json_t *R_lite;
    int version;
    int nentries;
    int nslots;
    struct entry *entries;
    int rc = -1;

    if (json_unpack_ex (R,
                        &error,
                        0,
                        "{s:i s:{s:o s?i}}",
                        "version", &version,
                        "execution",
                          "R_lite", &R_lite,
                          "nslots", &sm->nslots_R) < 0)
        return errprintf (errp, "%s", error.text);
    if (version != 1)
        return errprintf (errp,
                          "unsupported R version %d",
                          version);
    if (!json_is_array (R_lite))
        return errprintf (errp, "R_lite is not an array");

    nentries = json_array_size (R_lite);
    if (!(entries = calloc (nentries + 1, sizeof (*entries))))
        return errprintf (errp, "out of memory");
    if ((nslots = entries_parse (R_lite, entries, errp)) < 0)
        goto out;
    if (nslots > 0 && sm->nslots_R > 0 && nslots != sm->nslots_R) {
        errprintf (errp,
                   "sum of slot counts (%d) does not equal nslots (%d)",
                   nslots,
                   sm->nslots_R);
        goto out;
    }
    if (ranks_create (sm, entries, nentries, errp) < 0)
        goto out;
    if (nslots > 0) {
        for (int i = 0; i < nentries; i++) {
            if (groups_create_from_entry (sm, &entries[i]) < 0) {
                errprintf (errp, "out of memory");
                goto out;
            }
        }
        /* Order slots by core id, not by the order of keys in R
         */
        for (int i = 0; i < sm->nranks; i++) {
            struct rankinfo *ri = &sm->ranks[i];
            qsort (ri->groups, ri->ngroups, sizeof (*ri->groups), cmp_group);
        }
        sm->has_slots = true;
    }
    rc = 0;
out:
    for (int i = 0; i < nentries; i++)
        entry_clear (&entries[i]);
    free (entries);
    return rc;
}

struct slotmap *slotmap_create (json_t *R, flux_error_t *errp)
{
    struct slotmap *sm;

    if (!R) {
        errprintf (errp, "Invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(sm = calloc (1, sizeof (*sm)))
        || !(sm->labels = json_object ())) {
        errprintf (errp, "out of memory");
        free (sm);
        errno = ENOMEM;
        return NULL;
    }
    if (slotmap_load (sm, R, errp) < 0) {
        slotmap_destroy (sm);
        errno = EINVAL;
        return NULL;
    }
    return sm;
}

bool slotmap_has_slots (struct slotmap *sm)
{
    return sm ? sm->has_slots : false;
}

int slotmap_total_nodes (struct slotmap *sm)
{
    return sm ? sm->nranks : 0;
}

int slotmap_total_cores (struct slotmap *sm)
{
    return sm ? sm->ncores : 0;
}

int slotmap_total_gpus (struct slotmap *sm)
{
    return sm ? sm->ngpus : 0;
}

int slotmap_total_ntasks (struct slotmap *sm)
{
    return sm ? sm->ntasks : 0;
}

/* Return true if `label` is in the NULL terminated array `labels`, or
 * if `labels` is NULL.
 */
static bool label_match (const char **labels, const char *label)
{
    if (!labels)
        return true;
    for (int i = 0; labels[i] != NULL; i++) {
        if (streq (labels[i], label))
            return true;
    }
    return false;
}

int slotmap_count_slots (struct slotmap *sm, const char *label)
{
    const char *labels[] = { label, NULL };
    return slotmap_count_slots_labels (sm, label ? labels : NULL);
}

int slotmap_count_slots_labels (struct slotmap *sm, const char **labels)
{
    int count = 0;
    if (sm) {
        for (int i = 0; i < sm->nranks; i++) {
            struct rankinfo *ri = &sm->ranks[i];
            for (int j = 0; j < ri->ngroups; j++) {
                if (label_match (labels, ri->groups[j].label))
                    count += ri->groups[j].count;
            }
        }
    }
    return count;
}

int slotmap_total_slots (struct slotmap *sm)
{
    if (!sm)
        return 0;
    if (sm->nslots_R > 0)
        return sm->nslots_R;
    return sm->has_slots ? slotmap_count_slots (sm, NULL) : 0;
}

int slotmap_nodeid (struct slotmap *sm, int rank)
{
    struct rankinfo *ri;
    if (!sm || !(ri = rankinfo_find (sm, rank))) {
        errno = ENOENT;
        return -1;
    }
    return ri->nodeid;
}

struct rr {
    int nodeid;
    int ncores;
    int count;
};

/* Sort by count, then by number of cores, largest first, then by nodeid.
 */
static int cmp_rr (const void *a, const void *b)
{
    const struct rr *x = a;
    const struct rr *y = b;
    if (x->count != y->count)
        return y->count - x->count;
    if (x->ncores != y->ncores)
        return y->ncores - x->ncores;
    return x->nodeid - y->nodeid;
}

/* Deal `total` items to ranks round-robin, where rank i accepts up to
 * cap[i] items per pass. Each pass deals one item per round to each rank
 * with room left, ranks with the largest cap (then most cores) first.
 * When all ranks are full, start a new pass. Return the number of items
 * dealt to each rank in result[].
 */
static int deal_round_robin (struct slotmap *sm,
                             const int *cap,
                             int total,
                             int *result)
{
    struct rr *rr;
    long sum = 0;
    int npass;
    int extra;

    for (int i = 0; i < sm->nranks; i++)
        sum += cap[i];
    if (sum == 0) {
        errno = ENOENT;
        return -1;
    }
    if (!(rr = calloc (sm->nranks, sizeof (*rr))))
        return -1;
    for (int i = 0; i < sm->nranks; i++) {
        rr[i].nodeid = i;
        rr[i].ncores = idset_count (sm->ranks[i].cores);
        rr[i].count = cap[i];
    }
    qsort (rr, sm->nranks, sizeof (*rr), cmp_rr);

    /* Full passes, then deal the remainder one round at a time. Since rr
     * is sorted by count, the ranks with room in a round are a prefix.
     */
    npass = total / sum;
    extra = total % sum;
    for (int i = 0; i < sm->nranks; i++)
        result[i] = npass * cap[i];
    for (int round = 0; extra > 0; round++) {
        for (int i = 0; i < sm->nranks && extra > 0; i++) {
            if (rr[i].count <= round)
                break;
            result[rr[i].nodeid]++;
            extra--;
        }
    }
    free (rr);
    return 0;
}

struct avail {
    int nodeid;
    int ncores_avail;
};

/* N.B. This comparison and the use of zlist_sort() below must not change,
 * since together they determine the order of ranks with an equal number of
 * available cores, and therefore the placement of tasks by the shell.
 */
static int cmp_avail (void *a, void *b)
{
    const struct avail *x = a;
    const struct avail *y = b;
    return x->ncores_avail < y->ncores_avail;
}

static zlist_t *avail_list_sorted (struct avail *avail, int n)
{
    zlist_t *l;

    if (!(l = zlist_new ()))
        return NULL;
    for (int i = 0; i < n; i++) {
        if (zlist_append (l, &avail[i]) < 0) {
            zlist_destroy (&l);
            return NULL;
        }
    }
    zlist_sort (l, (zlist_compare_fn *) cmp_avail);
    return l;
}

/* Place `nslots` slots of `size` cores on ranks, and return the number
 * of slots on each rank in `count`. This is the heuristic of the original
 * shell task distribution (rcalc): take ranks largest first, place one
 * slot on a rank and move it to the back of the list, and drop ranks that
 * cannot fit another slot. Once the list is empty, start over with all
 * cores available, i.e. oversubscribe.
 */
static int place_slots (struct slotmap *sm, int nslots, int size, int *count)
{
    struct avail *avail;
    zlist_t *l = NULL;
    struct avail *a;
    int placed = 0;
    int rc = -1;

    if (!(avail = calloc (sm->nranks + 1, sizeof (*avail))))
        return -1;
    while (placed < nslots) {
        if (!l || !(a = zlist_pop (l))) {
            zlist_destroy (&l);
            for (int i = 0; i < sm->nranks; i++) {
                avail[i].nodeid = i;
                avail[i].ncores_avail = idset_count (sm->ranks[i].cores);
            }
            if (!(l = avail_list_sorted (avail, sm->nranks)))
                goto out;
            continue;
        }
        if (a->ncores_avail >= size) {
            a->ncores_avail -= size;
            count[a->nodeid]++;
            placed++;
            if (zlist_append (l, a) < 0)
                goto out;
        }
    }
    rc = 0;
out:
    zlist_destroy (&l);
    free (avail);
    return rc;
}

int slotmap_set_slots_even (struct slotmap *sm,
                            const char *label,
                            int nslots,
                            int cores_per_slot,
                            int gpus_per_slot,
                            flux_error_t *errp)
{
    int *count = NULL;
    int min_cores = INT_MAX;
    int rc = -1;

    if (!sm || nslots <= 0 || sm->nranks == 0) {
        errno = EINVAL;
        return errprintf (errp, "Invalid argument");
    }
    if (cores_per_slot <= 0
        && (cores_per_slot = sm->ncores / nslots) == 0) {
        errno = EINVAL;
        return errprintf (errp,
                          "unable to fit %d slots on %d cores",
                          nslots,
                          sm->ncores);
    }
    if (gpus_per_slot < 0)
        gpus_per_slot = 0;

    /* Ensure a slot fits on the smallest rank.
     */
    for (int i = 0; i < sm->nranks; i++) {
        int ncores = idset_count (sm->ranks[i].cores);
        if (ncores < min_cores)
            min_cores = ncores;
    }
    if (min_cores < cores_per_slot)
        cores_per_slot = min_cores;

    if (!(count = calloc (sm->nranks + 1, sizeof (*count)))
        || place_slots (sm, nslots, cores_per_slot, count) < 0) {
        errprintf (errp, "out of memory");
        goto out;
    }
    slots_clear (sm);
    for (int i = 0; i < sm->nranks; i++) {
        struct rankinfo *ri = &sm->ranks[i];
        struct group *g;
        if (count[i] == 0)
            continue;
        if (!(g = group_append (sm,
                                ri,
                                label,
                                count[i],
                                SPLIT_FIXED,
                                ri->cores,
                                ri->gpus))) {
            errprintf (errp, "out of memory");
            goto out;
        }
        g->core_size = cores_per_slot;
        g->gpu_size = gpus_per_slot;
    }
    rc = 0;
out:
    free (count);
    return rc;
}

int slotmap_set_slots_per_resource (struct slotmap *sm,
                                    const char *label,
                                    const char *type,
                                    flux_error_t *errp)
{
    bool by_core;

    if (!sm || !type) {
        errno = EINVAL;
        return errprintf (errp, "Invalid argument");
    }
    if (streq (type, "core"))
        by_core = true;
    else if (streq (type, "node"))
        by_core = false;
    else {
        errno = EINVAL;
        return errprintf (errp,
                          "invalid per-resource type %s",
                          type);
    }
    slots_clear (sm);
    for (int i = 0; i < sm->nranks; i++) {
        struct rankinfo *ri = &sm->ranks[i];
        int count = by_core ? idset_count (ri->cores) : 1;
        if (count > 0
            && !group_append (sm,
                              ri,
                              label,
                              count,
                              SPLIT_EVEN,
                              ri->cores,
                              ri->gpus))
            return errprintf (errp, "out of memory");
    }
    return 0;
}

int slotmap_distribute (struct slotmap *sm,
                        const char **labels,
                        int ntasks,
                        flux_error_t *errp)
{
    int *cap = NULL;
    int *result = NULL;
    int rc = -1;

    if (!sm || ntasks < 0) {
        errno = EINVAL;
        return errprintf (errp, "Invalid argument");
    }
    if (labels && slotmap_count_slots_labels (sm, labels) == 0)
        labels = NULL;
    if (!(cap = calloc (sm->nranks + 1, sizeof (*cap)))
        || !(result = calloc (sm->nranks + 1, sizeof (*result)))) {
        errprintf (errp, "out of memory");
        goto out;
    }
    for (int i = 0; i < sm->nranks; i++) {
        struct rankinfo *ri = &sm->ranks[i];
        for (int j = 0; j < ri->ngroups; j++) {
            struct group *g = &ri->groups[j];
            g->active = label_match (labels, g->label);
            if (g->active)
                cap[i] += g->count;
        }
    }
    /* Assign one task per active slot round-robin, ranks with the most
     * active slots first. If there are more tasks than active slots,
     * oversubscribe the slots in the same order.
     */
    if (deal_round_robin (sm, cap, ntasks, result) < 0) {
        if (errno == ENOENT)
            errprintf (errp, "no slots");
        else
            errprintf (errp, "out of memory");
        goto out;
    }
    for (int i = 0; i < sm->nranks; i++)
        sm->ranks[i].ntasks = result[i];
    sm->ntasks = ntasks;
    rc = 0;
out:
    free (cap);
    free (result);
    return rc;
}

int slotmap_update_map (struct slotmap *sm, const struct taskmap *map)
{
    if (!sm || !map) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < sm->nranks; i++) {
        int ntasks = taskmap_ntasks (map, i);
        /* taskmap_ntasks() fails with ENOENT for a node with no tasks
         */
        if (ntasks < 0) {
            if (errno != ENOENT) {
                errno = EINVAL;
                return -1;
            }
            ntasks = 0;
        }
        sm->ranks[i].ntasks = ntasks;
    }
    sm->ntasks = taskmap_total_ntasks (map);
    return 0;
}

/* Keep only the first `n` ids of `*ids`, if it has more.
 */
static int idset_truncate (struct idset **ids, int n)
{
    struct idset *new;

    if (idset_count (*ids) <= n)
        return 0;
    if (!(new = idset_slice (*ids, 0, n)))
        return -1;
    idset_destroy (*ids);
    *ids = new;
    return 0;
}

/* Reduce the resources of `ri` to the union of its active slots with
 * tasks. The active slots with tasks are the first ones, and slots from
 * R take resources in order, so this is a prefix of each group.
 */
static int rank_normalize_slots (struct rankinfo *ri)
{
    struct idset *cores = idset_create (0, IDSET_FLAG_AUTOGROW);
    struct idset *gpus = idset_create (0, IDSET_FLAG_AUTOGROW);
    bool all;
    int nactive = rank_nactive (ri, &all);
    int remaining = ri->ntasks < nactive ? ri->ntasks : nactive;
    int rc = -1;

    if (!cores || !gpus)
        goto out;
    for (int j = 0; j < ri->ngroups && remaining > 0; j++) {
        struct group *g = &ri->groups[j];
        int core_first, ncores, gpu_first, ngpus;
        struct idset *c;
        struct idset *u;
        int m;

        if (!all && !g->active)
            continue;
        m = remaining < g->count ? remaining : g->count;
        group_slot_range (g, m - 1, &core_first, &ncores, &gpu_first, &ngpus);
        c = idset_slice (g->cores, 0, core_first + ncores);
        u = idset_slice (g->gpus, 0, gpu_first + ngpus);
        if (!c || !u || idset_add (cores, c) < 0 || idset_add (gpus, u) < 0) {
            idset_destroy (c);
            idset_destroy (u);
            goto out;
        }
        idset_destroy (c);
        idset_destroy (u);
        remaining -= m;
    }
    idset_destroy (ri->cores);
    idset_destroy (ri->gpus);
    ri->cores = cores;
    ri->gpus = gpus;
    cores = gpus = NULL;
    rc = 0;
out:
    idset_destroy (cores);
    idset_destroy (gpus);
    return rc;
}

int slotmap_normalize (struct slotmap *sm,
                       int cores_per_task,
                       int gpus_per_task)
{
    if (!sm) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < sm->nranks; i++) {
        struct rankinfo *ri = &sm->ranks[i];
        /* With slots from R, keep the resources of slots with tasks.
         * Otherwise, keep the resources per task with the lowest ids, as
         * the original shell task distribution (rcalc) did.
         */
        if (sm->has_slots) {
            if (rank_normalize_slots (ri) < 0)
                return -1;
        }
        else if (idset_truncate (&ri->cores, ri->ntasks * cores_per_task) < 0
                 || idset_truncate (&ri->gpus,
                                    ri->ntasks * gpus_per_task) < 0)
            return -1;
    }
    return 0;
}

int slotmap_get_nth (struct slotmap *sm,
                     int nodeid,
                     struct slotmap_rankinfo *rli)
{
    struct rankinfo *ri;

    if (!sm || !rli || nodeid < 0 || nodeid >= sm->nranks) {
        errno = EINVAL;
        return -1;
    }
    ri = &sm->ranks[nodeid];
    rli->nodeid = ri->nodeid;
    rli->rank = ri->rank;
    rli->ntasks = ri->ntasks;
    rli->ncores = idset_count (ri->cores);
    return 0;
}

static char *encode (struct slotmap *sm,
                     int nodeid,
                     bool gpus)
{
    struct rankinfo *ri;

    if (!sm || nodeid < 0 || nodeid >= sm->nranks) {
        errno = EINVAL;
        return NULL;
    }
    ri = &sm->ranks[nodeid];
    return idset_encode (gpus ? ri->gpus : ri->cores, IDSET_FLAG_RANGE);
}

char *slotmap_cores (struct slotmap *sm, int nodeid)
{
    return encode (sm, nodeid, false);
}

char *slotmap_gpus (struct slotmap *sm, int nodeid)
{
    return encode (sm, nodeid, true);
}

/* Return a slot object for slot `i` of group `g` with `ntasks` tasks.
 */
static json_t *slot_json (struct group *g, int i, int ntasks)
{
    struct idset *cores = NULL;
    struct idset *gpus = NULL;
    char *c = NULL;
    char *u = NULL;
    json_t *o = NULL;

    if (group_slot (g, i, &cores, &gpus) == 0
        && (c = idset_encode (cores, IDSET_FLAG_RANGE))
        && (u = idset_encode (gpus, IDSET_FLAG_RANGE)))
        o = json_pack ("{s:s s:i s:s s:s}",
                       "label", g->label,
                       "ntasks", ntasks,
                       "cores", c,
                       "gpus", u);
    idset_destroy (cores);
    idset_destroy (gpus);
    free (c);
    free (u);
    return o;
}

json_t *slotmap_slots_json (struct slotmap *sm, int nodeid)
{
    struct rankinfo *ri;
    json_t *a;
    bool all;
    int nactive;
    int p = 0;

    if (!sm || nodeid < 0 || nodeid >= sm->nranks) {
        errno = EINVAL;
        return NULL;
    }
    ri = &sm->ranks[nodeid];
    nactive = rank_nactive (ri, &all);
    if (!(a = json_array ()))
        goto nomem;
    for (int j = 0; j < ri->ngroups; j++) {
        struct group *g = &ri->groups[j];
        bool active = all || g->active;
        for (int i = 0; i < g->count; i++) {
            int ntasks = active ? active_slot_ntasks (ri, nactive, p++) : 0;
            json_t *o;
            if (!(o = slot_json (g, i, ntasks))
                || json_array_append_new (a, o) < 0) {
                json_decref (o);
                goto nomem;
            }
        }
    }
    return a;
nomem:
    json_decref (a);
    errno = ENOMEM;
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
