/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* N.B. This was split from rlist.c that rlist.o can be used in a limited
 * fashion by sched-simple and job-list without inheriting a hwloc dependency.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "rnode.h"
#include "rlist.h"
#include "rlist_private.h"
#include "rhwloc.h"

struct rlist *rlist_from_hwloc (int rank, const char *xml)
{
    char *ids = NULL;
    struct rnode *n = NULL;
    hwloc_topology_t topo = NULL;
    const char *name;
    struct rlist *rl = rlist_create ();

    if (!rl)
        return NULL;

    if (xml)
        topo = rhwloc_xml_topology_load (xml);
    else
        topo = rhwloc_local_topology_load (0);
    if (!topo)
        goto fail;
    if (!(ids = rhwloc_core_idset_string (topo))
        || !(name = rhwloc_hostname (topo)))
        goto fail;

    if (!(n = rnode_create (name, rank, ids))
        || rlist_add_rnode (rl, n) < 0)
        goto fail;

    free (ids);

    if ((ids = rhwloc_gpu_idset_string (topo))
        && rnode_add_child (n, "gpu", ids) < 0)
        goto fail;

    hwloc_topology_destroy (topo);
    free (ids);
    return rl;
fail:
    rlist_destroy (rl);
    rnode_destroy (n);
    free (ids);
    hwloc_topology_destroy (topo);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
