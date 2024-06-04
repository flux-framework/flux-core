/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_TOPOLOGY_H
#define _BROKER_TOPOLOGY_H

#include <sys/types.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/idset.h>

/* Create/destroy tree topology of size.
 * The default topology is "flat" (rank 0 is parent of all other ranks),
 * and queries are from the perspective of rank 0.
 * If uri is non-NULL, the scheme selects a topology type, and the path
 * provides additional detail.  The following schemes are available:
 *
 * kary:K
 * Set the topology to a complete k-ary tree with fanout K.
 */
struct topology *topology_create (const char *uri,
                                  int size,
                                  flux_error_t *error);
void topology_decref (struct topology *topo);
struct topology *topology_incref (struct topology *topo);

/* Set "my rank", which provides the point of view for queries.
 */
int topology_set_rank (struct topology *topo, int rank);

/* Associate aux data with rank for lookup in O(1*rank_aux_elements)
 */
void *topology_rank_aux_get (struct topology *topo, int rank, const char *name);
int topology_rank_aux_set (struct topology *topo,
                           int rank,
                           const char *name,
                           void *aux,
                           flux_free_f destroy);

/* Queries
 */
int topology_get_rank (struct topology *topo);
int topology_get_size (struct topology *topo);
int topology_get_parent (struct topology *topo);
ssize_t topology_get_child_ranks (struct topology *topo,
                                  int *child_ranks,
                                  size_t child_ranks_length);
int topology_get_level (struct topology *topo);
int topology_get_maxlevel (struct topology *topo);
int topology_get_descendant_count (struct topology *topo);
int topology_get_descendant_count_at (struct topology *topo, int rank);
int topology_get_child_route (struct topology *topo, int rank);
json_t *topology_get_json_subtree_at (struct topology *topo, int rank);

/*  Return internal ranks (ranks that have one or more children)
 */
struct idset *topology_get_internal_ranks (struct topology *topo);

void topology_hosts_set (json_t *hosts);

/* Plugins
 */
struct topology_plugin {
    const char *name;
    int (*init)(struct topology *topo, const char *path, flux_error_t *error);
};

#endif /* !_BROKER_TOPOLOGY_H */

// vi:ts=4 sw=4 expandtab
