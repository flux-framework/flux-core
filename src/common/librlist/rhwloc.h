/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_UTIL_RHWLOC_H
#define HAVE_UTIL_RHWLOC_H 1

#include <hwloc.h>

#include "src/common/libflux/types.h" /* flux_error_t */

typedef enum {
    RHWLOC_NO_RESTRICT = 0x1
} rhwloc_flags_t;

/*  Load local topology with Flux standard flags and filtering
 */
hwloc_topology_t rhwloc_local_topology_load (rhwloc_flags_t flags);

/*  As above, but return hwloc_topoology_t from XML
 *  Topology is restricted to current CPU binding unless RHWLOC_NO_RESTRICT
 *  flag is used.
 */
hwloc_topology_t rhwloc_xml_topology_load (const char *xml,
                                           rhwloc_flags_t flags);

/*  Load local topology and return XML as allocated string
 */
char *rhwloc_local_topology_xml (rhwloc_flags_t flags);

/*  Restrict an XML topology to current CPU binding and return result.
 */
char *rhwloc_topology_xml_restrict (const char *xml);

/*  Return HostName from an hwloc topology object
 */
const char *rhwloc_hostname (hwloc_topology_t topo);

/*  Return the union of cpusets for the cores in idset string `cores`.
 *  Returns heap-allocated hwloc_cpuset_t, or NULL on error.
 *  Caller must free with hwloc_bitmap_free().
 */
hwloc_cpuset_t rhwloc_cores_to_cpuset (hwloc_topology_t topo,
                                        const char *cores,
                                        flux_error_t *errp);

/*  Return idset string for all cores in hwloc topology object
 */
char * rhwloc_core_idset_string (hwloc_topology_t topo);

/*  Return heap-allocated array of unique compute GPU osdev objects from topo
 *  in hwloc traversal order, one per physical GPU (deduplicated by PCI
 *  ancestor).  Sets *count_out to the number of entries.  Returns NULL with
 *  *count_out == 0 when no GPUs are present.  Caller must free().
 */
hwloc_obj_t *rhwloc_gpu_objects (hwloc_topology_t topo, int *count_out);

/*  Return idset string for all GPUs in hwloc topology object
 */
char * rhwloc_gpu_idset_string (hwloc_topology_t topo);

/*  Return the count of resource "type" in topology topo. Takes any
 *  hwloc obj type string or "gpu" for supported GPU types.
 */
int rhwloc_count_type (hwloc_topology_t topo, const char *type);

/*  Return rlist object from local hwloc topology, or from xml if non-NULL.
 */
struct rlist *rlist_from_hwloc (int my_rank, const char *xml);

#endif /* !HAVE_UTIL_RHWLOC */
