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
#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"

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

static int kary_plugin_init (struct topology *topo,
                             const char *path,
                             flux_error_t *error);
static int mincrit_plugin_init (struct topology *topo,
                                const char *path,
                                flux_error_t *error);
static int binomial_plugin_init (struct topology *topo,
                                 const char *path,
                                 flux_error_t *error);
static int custom_plugin_init (struct topology *topo,
                               const char *path,
                               flux_error_t *error);

static const struct topology_plugin builtin_plugins[] = {
    { .name = "kary",   .init = kary_plugin_init },
    { .name = "mincrit", .init = mincrit_plugin_init },
    { .name = "binomial", .init = binomial_plugin_init },
    { .name = "custom", .init = custom_plugin_init },
};

static json_t *boot_hosts;

void topology_hosts_set (json_t *hosts)
{
    boot_hosts = hosts;
}

static const struct topology_plugin *topology_plugin_lookup (const char *name)
{
    for (int i = 0; i < ARRAY_SIZE (builtin_plugins); i++)
        if (streq (name, builtin_plugins[i].name))
            return &builtin_plugins[i];
    return NULL;
}

static int topology_plugin_call (struct topology *topo,
                                 const char *uri,
                                 flux_error_t *error)
{
    const struct topology_plugin *plugin;
    char *name;
    char *path;

    if (!(name = strdup (uri))) {
        errprintf (error, "out of memory");
        goto error;
    }
    if ((path = strchr (name, ':')))
        *path++ = '\0';
    if (!(plugin = topology_plugin_lookup (name))) {
        errprintf (error, "unknown topology scheme '%s'", name);
        goto error;
    }
    if (plugin->init (topo, path, error) < 0)
        goto error;
    free (name);
    return 0;
error:
    free (name);
    errno = EINVAL;
    return -1;
}

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

struct topology *topology_create (const char *uri,
                                  int size,
                                  flux_error_t *error)
{
    struct topology *topo;

    if (size < 1) {
        errprintf (error, "invalid topology size %d", size);
        errno = EINVAL;
        return NULL;
    }
    if (!(topo = calloc (1, sizeof (*topo) + sizeof (topo->node[0]) * size))) {
        errprintf (error, "out of memory");
        goto error;
    }
    topo->refcount = 1;
    topo->size = size;
    topo->node = (struct node *)(topo + 1);
    topo->node[0].parent = -1;
    // topo->node is 0-initialized, so rank 0 is default parent of all nodes

    if (uri) {
        if (topology_plugin_call (topo, uri, error) < 0)
            goto error;
    }
    return topo;
error:
    topology_decref (topo);
    return NULL;
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

void *topology_rank_aux_get (struct topology *topo, int rank, const char *name)
{
    if (!topo || rank < 0 || rank >= topo->size) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (topo->node[rank].aux, name);
}

int topology_rank_aux_set (struct topology *topo,
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

struct idset *topology_get_internal_ranks (struct topology *topo)
{
    struct idset *ranks;

    if (!topo) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    for (int i = 1; i < topo->size; i++) {
        if (idset_set (ranks, topo->node[i].parent) < 0)
            goto error;
    }
    return ranks;
error:
    idset_destroy (ranks);
    return NULL;
}

static int parse_k (const char *s, int *result)
{
    char *endptr;
    unsigned long val;

    if (!s || strlen (s) == 0)
        return -1;
    errno = 0;
    val = strtoul (s, &endptr, 10);
    if (errno != 0 || *endptr != '\0')
        return -1;
    *result = val;
    return 0;
}

/* kary plugin
 */
static int kary_plugin_init (struct topology *topo,
                             const char *path,
                             flux_error_t *error)
{
    int k;

    if (parse_k (path, &k) < 0) {
        errprintf (error, "kary k value must be an integer >= 0");
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

/* Given size and k (number of routers), determine fanout from
 * routers to leaves.
 */
static int mincrit_router_fanout (int size, int k)
{
    int crit = 1 + k;
    int leaves = size - crit;
    int fanout = leaves / k;
    if (leaves % k > 0)
        fanout++;
    return fanout;
}

/* Choose a value for k that balances minimizing critical nodes and
 * keeping the fanout from routers to leaves at or below a threshold.
 * The height is always capped at 3, so max_fanout might be exceeded
 * from leader to routers for large size or small max_fanout.
 * Do choose k=0 (flat tree) if max_fanount can be met by the leader node.
 * Don't choose k=1, since that just pushes some router work off
 * to rank 1, without tree benefits.
 */
static int mincrit_choose_k (int size, int max_fanout)
{
    int k = 0;
    if (size > max_fanout + 1) {
        k = 2;
        while (mincrit_router_fanout (size, k) > max_fanout)
            k++;
    }
    return k;
}

/* mincrit plugin
 * A k-ary tree "squashed" down to at most three levels.
 * The value of k determines the fanout from leader to routers.
 * The number of nodes determines the fanout from routers to leaves.
 * The value of k may be 0, or be unspecifed (letting the system choose).
 */
static int mincrit_plugin_init (struct topology *topo,
                                const char *path,
                                flux_error_t *error)
{
    int k;

    if (path && strlen (path) > 0) {
        if (parse_k (path, &k) < 0) {
            errprintf (error, "mincrit k value must be an integer >= 0");
            return -1;
        }
    }
    else {
        k = mincrit_choose_k (topo->size, 1024);
    }
    /* N.B. topo is initialized with rank 0 as the parent of all other ranks
     * before plugin init is called, therefore only the leaves need to have
     * their parent set here.
     */
    if (k > 0) {
        for (int i = k + 1; i < topo->size; i++)
            topo->node[i].parent = (i - k - 1) % k + 1;
    }
    return 0;
}

/* binomial plugin
 */
static int binomial_smallest_k (int size)
{
    size_t max_k = sizeof (int) * 8 - 1;

    for (int k = 0; k < max_k; k++) {
        int tree_size = 1 << k;
        if (size <= tree_size)
            return k;
    }
    return -1;
}

static void binomial_generate (struct topology *topo, int root, int k)
{
    for (int j = 0; j < k; j++) {
        int child = root + (1 << j);
        if (child < topo->size) {
            topo->node[child].parent = root;
            binomial_generate (topo, child, j);
        }
    }
}

static int binomial_plugin_init (struct topology *topo,
                                 const char *path,
                                 flux_error_t *error)
{
    int k;

    if (path && strlen (path) > 0) {
        errprintf (error, "unknown binomial topology directive: '%s'", path);
        return -1;
    }
    if ((k = binomial_smallest_k (topo->size)) < 0) {
        errprintf (error, "binomial: internal overflow");
        return -1;
    }
    binomial_generate (topo, 0, k);
    return 0;
}

/* custom plugin
 * Set rank-ordered hosts array with topology_hosts_set() before using.
 * Each entry has the form { "host":s "parent"?s }.
 */

/* Helper to find rank of 'hostname'
 */
static int gethostrank (const char *hostname, json_t *hosts, int *rank)
{
    size_t index;
    json_t *entry;
    const char *host;

    json_array_foreach (hosts, index, entry) {
        if (json_unpack (entry, "{s:s}", "host", &host) == 0
            && streq (host, hostname)) {
            *rank = index;
            return 0;
        }
    }
    return -1;
}

static int custom_plugin_init (struct topology *topo,
                               const char *path,
                               flux_error_t *error)
{
    size_t rank;
    json_t *entry;
    const char *parent;
    const char *host;
    int parent_rank;

    if (path && strlen (path) > 0) {
        errprintf (error, "unknown custom topology directive: '%s'", path);
        return -1;
    }
    if (!boot_hosts)
        return 0;
    json_array_foreach (boot_hosts, rank, entry) {
        if (json_unpack (entry,
                         "{s:s s:s}",
                         "host", &host,
                         "parent", &parent) < 0)
            continue;
        if (rank == 0) {
            errprintf (error,
                       "Config file [bootstrap] hosts:"
                       " rank 0 (%s) may not have a parent in a tree topology",
                       host);
            return -1;
        }
        if (rank >= topo->size) {
            errprintf (error, "topology size does not match host array size");
            return -1;
        }
        if (gethostrank (parent, boot_hosts, &parent_rank) < 0
            || parent_rank < 0 || parent_rank >= topo->size) {
            errprintf (error,
                       "Config file [bootstrap] hosts:"
                       " invalid parent \"%s\" for %s (rank %zu)",
                       parent,
                       host,
                       rank);
            return -1;
        }
        if (parent_rank == rank
            || is_descendant_of (topo, parent_rank, rank)) {
            errprintf (error,
                       "Config file [bootstrap] hosts: parent \"%s\""
                       " for %s (rank %zu) violates rule against cycles",
                       parent,
                       host,
                       rank);
            return -1;
        }
        topo->node[rank].parent = parent_rank;
    }
    return 0;
}

// vi:ts=4 sw=4 expandtab
