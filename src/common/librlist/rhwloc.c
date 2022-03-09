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

/*  Common hwloc_topology_init() and flags for Flux hwloc usage:
 */
static int topo_init_common (hwloc_topology_t *tp)
{
    if (hwloc_topology_init (tp) < 0)
        return -1;
#if HWLOC_API_VERSION < 0x20000
    /*  N.B.: hwloc_topology_set_flags may cause memory leaks on some systems
     */
    if (hwloc_topology_set_flags (*tp, HWLOC_TOPOLOGY_FLAG_IO_DEVICES) < 0)
        return -1;
    if (hwloc_topology_ignore_type (*tp, HWLOC_OBJ_CACHE) < 0)
        return -1;
#else
    if (hwloc_topology_set_io_types_filter(*tp,
                                           HWLOC_TYPE_FILTER_KEEP_IMPORTANT)
        < 0)
        return -1;
    if (hwloc_topology_set_cache_types_filter(*tp,
                                              HWLOC_TYPE_FILTER_KEEP_STRUCTURE)
        < 0)
        return -1;
    if (hwloc_topology_set_icache_types_filter(*tp,
                                               HWLOC_TYPE_FILTER_KEEP_STRUCTURE)
        < 0)
        return -1;
#endif
    return 0;
}

static int init_topo_from_xml (hwloc_topology_t *tp, const char *xml)
{
    if ((topo_init_common (tp) < 0)
        || (hwloc_topology_set_xmlbuffer (*tp, xml, strlen (xml) + 1) < 0)
        || (hwloc_topology_load (*tp) < 0)) {
        hwloc_topology_destroy (*tp);
        return (-1);
    }
    return (0);
}

hwloc_topology_t rhwloc_xml_topology_load (const char *xml)
{
    hwloc_topology_t topo = NULL;
    if (init_topo_from_xml (&topo, xml) < 0)
        return NULL;
    return topo;
}

hwloc_topology_t rhwloc_local_topology_load (void)
{
    hwloc_topology_t topo = NULL;
    hwloc_bitmap_t rset = NULL;
    uint32_t hwloc_version = hwloc_get_api_version ();

    if ((hwloc_version >> 16) != (HWLOC_API_VERSION >> 16))
        return NULL;

    if (topo_init_common (&topo) < 0)
        goto err;
#if HWLOC_API_VERSION >= 0x20100
    /* gl probes the NV-CONTROL X server extension, and requires X auth
     * to be properly set up or errors are emitted to stderr.
     * Nvidia GPUs can still be discovered via opencl.
     */
    hwloc_topology_set_components (topo,
                                   HWLOC_TOPOLOGY_COMPONENTS_FLAG_BLACKLIST,
                                   "gl");
#endif
    if (hwloc_topology_load (topo) < 0)
        goto err;
    if (!(rset = hwloc_bitmap_alloc ())
        || (hwloc_get_cpubind (topo, rset, HWLOC_CPUBIND_PROCESS) < 0))
        goto err;
    if (hwloc_topology_restrict (topo, rset, 0) < 0)
        goto err;
    hwloc_bitmap_free (rset);
    return (topo);
err:
    hwloc_bitmap_free (rset);
    hwloc_topology_destroy (topo);
    return NULL;
}

char *rhwloc_local_topology_xml (void)
{
    char *buf;
    int buflen;
    char *copy;
    hwloc_topology_t topo = rhwloc_local_topology_load ();
    if (topo == NULL)
        return (NULL);
#if HWLOC_API_VERSION >= 0x20000
    int flags = HWLOC_TOPOLOGY_EXPORT_XML_FLAG_V1;
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen, flags) < 0) {
#else
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen) < 0) {
#endif
        return NULL;
    }
    copy = strdup (buf);
    hwloc_free_xmlbuffer (topo, buf);
    hwloc_topology_destroy (topo);
    return (copy);
}

const char * rhwloc_hostname (hwloc_topology_t topo)
{
        int depth = hwloc_get_type_depth (topo, HWLOC_OBJ_MACHINE);
    hwloc_obj_t obj = hwloc_get_obj_by_depth (topo, depth, 0);
    if (obj)
        return hwloc_obj_get_info_by_name(obj, "HostName");
    return NULL;
}

/*  Generate a cpuset string for all cores in the current topology
 */
char *rhwloc_core_idset_string (hwloc_topology_t topo)
{
    char *result = NULL;
    struct idset *ids = NULL;
    int depth = hwloc_get_type_depth (topo, HWLOC_OBJ_CORE);

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto out;

    for (int i = 0; i < hwloc_get_nbobjs_by_depth(topo, depth); i++) {
        hwloc_obj_t core = hwloc_get_obj_by_depth (topo, depth, i);
        idset_set (ids, core->logical_index);
    }

    result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

/*  Return true if the hwloc "backend" type string matches a GPU
 *   which should be indexed as a compute GPU.
 */
static bool backend_is_coproc (const char *s)
{
    /* Only count cudaX, openclX, and rmsiX devices for now */
    return (strcmp (s, "CUDA") == 0
            || strcmp (s, "OpenCL") == 0
            || strcmp (s, "RSMI") == 0);
}

char * rhwloc_gpu_idset_string (hwloc_topology_t topo)
{
    int index;
    char *result = NULL;
    hwloc_obj_t obj = NULL;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);

    if (!ids)
        return NULL;

    /*  Manually index GPUs -- os_index does not seem to be valid for
     *  these devices in some cases, and logical index also seems
     *  incorrect (?)
     */
    index = 0;
    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && backend_is_coproc (s))
            idset_set (ids, index++);
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
    idset_destroy (ids);
    return result;
}

/* vi: ts=4 sw=4 expandtab
 */
