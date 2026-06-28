/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_RHWLOC_TREEPOOL_H
#define HAVE_RHWLOC_TREEPOOL_H 1

#include <jansson.h>
#include <hwloc.h>

#include "src/common/libflux/types.h" /* flux_error_t */

/*  Build TreePool topology object (RFC 49) from hwloc topology.
 *  Returns a JSON object describing the node's sub-node resource layout
 *  (cores, GPUs, memory organized by socket/NUMA/etc).  Caller must
 *  json_decref().  Returns NULL on failure with errno set and errp filled
 *  if non-NULL.
 */
json_t *rhwloc_treepool_topo (hwloc_topology_t topo, flux_error_t *errp);

/*  Convenience wrapper: load hwloc topology from XML and convert to TreePool
 *  JSON string.  Loads topology without CPU binding restriction (so the full
 *  node topology is visible).  Caller must free() the returned string.
 *  Returns NULL on failure with errno set and errp filled if non-NULL.
 */
char *rhwloc_treepool_topo_to_json (const char *xml, flux_error_t *errp);

#endif /* !HAVE_RHWLOC_TREEPOOL_H */
