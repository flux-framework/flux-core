/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include <flux/idset.h>

#include "hwloc.h"

char *xml_topology_get (hwloc_topology_t topo)
{
    char *buf;
    int buflen;

#if HWLOC_API_VERSION >= 0x20000
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen,
                                         HWLOC_TOPOLOGY_EXPORT_XML_FLAG_V1) < 0)
        return NULL;
#else
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen) < 0)
	return NULL;
#endif
    //assert (buf[buflen - 1] == '\0');
    return buf;
}
