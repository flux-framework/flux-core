/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* topology.c - create arbitrary TBON topology and allow useful queries */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libutil/kary.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/aux.h"

#include "topology.h"

struct node {
    int parent;
    struct aux_item *aux;
};

struct topology {
    int rank;
    int size;
    int refcount;
    struct node *node;
};

void topology_decref (struct topology *topo)
{
    if (topo && --topo->refcount == 0) {
        int saved_errno = errno;
        for (int i = 0; i < topo->size; i++)
            aux_destroy (&topo->node[i].aux);
        free (topo);
        errno = saved_errno;
    }
}

struct topology *topology_incref (struct topology *topo)
{
    if (topo)
        topo->refcount++;
    return topo;
}

struct topology *topology_create (int size)
{
    struct topology *topo;

    if (size < 1) {
        errno = EINVAL;
        return NULL;
    }
    if (!(topo = calloc (1, sizeof (*topo) + sizeof (topo->node[0]) * size)))
        return NULL;
    topo->refcount = 1;
    topo->size = size;
    topo->node = (struct node *)(topo + 1);
    topo->node[0].parent = -1;
    // topo->node is 0-initialized, so rank 0 is default parent of all nodes
    return topo;
}

int topology_set_kary (struct topology *topo, int k)
{
    if (!topo || k < 0) {
        errno = EINVAL;
        return -1;
    }
    if (k > 0) {
        for (int i = 0; i < topo->size; i++) {
            topo->node[i].parent = kary_parentof (k, i);
            if (topo->node[i].parent == KARY_NONE)
                topo->node[i].parent = -1;
        }
    }
    return 0;
}

int topology_set_rank (struct topology *topo, int rank)
{
    if (!topo || rank < 0 || rank >= topo->size) {
        errno = EINVAL;
        return -1;
    }
    topo->rank = rank;
    return 0;
}

void *topology_aux_get (struct topology *topo, int rank, const char *name)
{
    if (!topo || rank < 0 || rank >= topo->size) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (topo->node[rank].aux, name);
}

int topology_aux_set (struct topology *topo,
                      int rank,
                      const char *name,
                      void *val,
                      flux_free_f destroy)
{
    if (!topo || rank < 0 || rank >= topo->size) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&topo->node[rank].aux, name, val, destroy);
}

int topology_get_rank (struct topology *topo)
{
    return topo ? topo->rank : -1;
}

int topology_get_size (struct topology *topo)
{
    return topo ? topo->size : -1;
}

// O(1)
int topology_get_parent (struct topology *topo)
{
    return topo ? topo->node[topo->rank].parent : -1;
}

// O(size)
static ssize_t topology_get_child_ranks_at (struct topology *topo,
                                            int rank,
                                            int *child_ranks,
                                            size_t child_ranks_length)
{
    ssize_t count = 0;

    if (!topo
        || rank < 0
        || rank >= topo->size
        || (child_ranks_length > 0 && child_ranks == NULL)) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < topo->size; i++) {
        if (topo->node[i].parent == rank) {
            if (child_ranks) {
                if (count >= child_ranks_length) {
                    errno = EOVERFLOW;
                    return -1;
                }
                child_ranks[count] = i;
            }
            count++;
        }
    }

    return count;
}

ssize_t topology_get_child_ranks (struct topology *topo,
                                  int *child_ranks,
                                  size_t child_ranks_length)
{
    if (!topo) {
        errno = EINVAL;
        return -1;
    }
    return topology_get_child_ranks_at (topo,
                                        topo->rank,
                                        child_ranks,
                                        child_ranks_length);
}

// O(level)
int topology_get_level (struct topology *topo)
{
    int level = 0;

    if (topo) {
        int rank = topo->rank;
        while (rank != 0) {
            rank = topo->node[rank].parent;
            level++;
        }
    }
    return level;
}

// O(size*level)
int topology_get_maxlevel (struct topology *topo)
{
    int maxlevel = 0;

    if (topo) {
        for (int i = 0; i < topo->size; i++) {
            int rank = i;
            int level = 0;
            while (rank != 0) {
                rank = topo->node[rank].parent;
                level++;
            }
            if (maxlevel < level)
                maxlevel = level;
        }
    }
    return maxlevel;
}

// O(level)
static bool is_descendant_of (struct topology *topo, int rank, int ancestor)
{
    if (rank < 0
        || ancestor < 0
        || !topo
        || rank >= topo->size
        || ancestor >= topo->size
        || topo->node[rank].parent == -1)
        return false;
    if (topo->node[rank].parent == ancestor)
        return true;
    return is_descendant_of (topo, topo->node[rank].parent, ancestor);
}

// O(size*level)
int topology_get_descendant_count_at (struct topology *topo, int rank)
{
    int count = 0;
    if (topo) {
        for (int i = 0; i < topo->size; i++) {
            if (is_descendant_of (topo, i, rank))
                count++;
        }
    }
    return count;
}

int topology_get_descendant_count (struct topology *topo)
{
    return topology_get_descendant_count_at (topo, topo ? topo->rank : 0);
}

// O(level)
int topology_get_child_route (struct topology *topo, int rank)
{
    if (!topo || rank <= 0 || rank >= topo->size)
        return -1;
    if (topo->node[rank].parent == topo->rank)
        return rank;
    return topology_get_child_route (topo, topo->node[rank].parent);
}

json_t *topology_get_json_subtree_at (struct topology *topo, int rank)
{
    int child_count;
    int *child_ranks = NULL;
    json_t *o;
    json_t *children = NULL;
    json_t *child;
    int size;

    if ((child_count = topology_get_child_ranks_at (topo, rank, NULL, 0)) < 0
        || !(child_ranks = calloc (child_count, sizeof (child_ranks[0])))
        || topology_get_child_ranks_at (topo,
                                        rank,
                                        child_ranks,
                                        child_count) < 0)
        goto error;
    if (!(children = json_array()))
        goto nomem;
    for (int i = 0; i < child_count; i++) {
        if (!(child = topology_get_json_subtree_at (topo, child_ranks[i])))
            goto error;
        if (json_array_append_new (children, child) < 0) {
            json_decref (child);
            goto nomem;
        }
    }

    size = topology_get_descendant_count_at (topo, rank) + 1;
    if (!(o = json_pack ("{s:i s:i s:O}",
                         "rank", rank,
                         "size", size,
                         "children", children)))
        goto nomem;
    json_decref (children);
    free (child_ranks);
    return o;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, children);
    ERRNO_SAFE_WRAP (free, child_ranks);
    return NULL;

}

// vi:ts=4 sw=4 expandtab
