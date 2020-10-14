/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_RESOURCE_HWLOC_H
#define HAVE_RESOURCE_HWLOC_H 1

#include <hwloc.h>

/* free result with hwloc_free_xmlbuffer() */
char *xml_topology_get (hwloc_topology_t topo);

#endif /* !HAVE_RESOURCE_HWLOC_H */
