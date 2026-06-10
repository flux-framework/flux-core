/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rhwloc_scheduling.c - build R scheduling key from hwloc topology */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <string.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"
#include "rhwloc.h"

/* Internal: implemented in rhwloc_scheduling_treepool.c */
json_t *rhwloc_scheduling_treepool (hwloc_topology_t topo, flux_error_t *errp);

json_t *rhwloc_scheduling (hwloc_topology_t topo,
                            const char *format,
                            const char *ranks,
                            flux_error_t *errp)
{
    json_t *topo_obj = NULL;
    json_t *entry = NULL;
    json_t *children = NULL;
    json_t *result = NULL;
    const char *writer;

    if (strcasecmp (format, "TreePool") == 0) {
        topo_obj = rhwloc_scheduling_treepool (topo, errp);
        writer = "TreePool";
    }
    else {
        errprintf (errp, "unknown scheduling format: %s", format);
        errno = EINVAL;
        return NULL;
    }
    if (!topo_obj) {
        if (errno == 0)
            errno = EINVAL;
        return NULL;
    }

    if (!(entry = json_pack ("{s:s, s:o}", "ranks", ranks, "topo", topo_obj)))
        goto error;
    topo_obj = NULL; /* owned by entry */

    if (!(children = json_array ())
        || json_array_append_new (children, entry) < 0)
        goto error;
    entry = NULL; /* owned by children */

    if (!(result = json_pack ("{s:s, s:o}",
                              "writer", writer,
                              "children", children)))
        goto error;
    return result;

error:
    json_decref (topo_obj);
    json_decref (entry);
    json_decref (children);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
