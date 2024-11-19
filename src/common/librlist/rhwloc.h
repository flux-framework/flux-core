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

/*  Return idset string for all cores in hwloc topology object
 */
char * rhwloc_core_idset_string (hwloc_topology_t topo);

/*  Return idset string for all GPUs in hwloc topology object
 */
char * rhwloc_gpu_idset_string (hwloc_topology_t topo);

/*  Return rlist object from local hwloc topology, or from xml if non-NULL.
 */
struct rlist *rlist_from_hwloc (int my_rank, const char *xml);

#endif /* !HAVE_UTIL_RHWLOC */
