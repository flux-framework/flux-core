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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/utsname.h>

#include <flux/idset.h>

#include "ccan/str/str.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/errno_safe.h"

#include "rnode.h"
#include "rlist.h"
#include "rlist_private.h"
#include "rhwloc.h"


/*  Common hwloc_topology_init() and flags for Flux hwloc usage:
 */
static int topo_init_common (hwloc_topology_t *tp, unsigned long flags)
{
    if (hwloc_topology_init (tp) < 0)
        return -1;
#if HWLOC_API_VERSION < 0x20000
    flags |= HWLOC_TOPOLOGY_FLAG_IO_DEVICES;
    if (hwloc_topology_ignore_type (*tp, HWLOC_OBJ_CACHE) < 0)
        return -1;
#else
    if (hwloc_topology_set_io_types_filter(*tp,
                                           HWLOC_TYPE_FILTER_KEEP_IMPORTANT)
        < 0)
        return -1;
    if (hwloc_topology_set_icache_types_filter(*tp,
                                               HWLOC_TYPE_FILTER_KEEP_STRUCTURE)
        < 0)
        return -1;
#endif
    /*  N.B.: hwloc_topology_set_flags may cause memory leaks on some systems
     */
    if (hwloc_topology_set_flags (*tp, flags) < 0)
        return -1;
    return 0;
}

static int init_topo_from_xml (hwloc_topology_t *tp,
                               const char *xml,
                               unsigned long flags)
{
    int len = strlen (xml) + 1;

    if (topo_init_common (tp, flags) < 0)
        return -1;
    if (hwloc_topology_set_xmlbuffer (*tp, xml, len) < 0) {
        /* In some versions of hwloc/libxml, the NUL character on the XML
         * buffer cannot be included in len. Therefore, if set_xmlbuffer fails
         * above, retry with len-1.
         */
        if (hwloc_topology_set_xmlbuffer (*tp, xml, len - 1) < 0)
            goto error;
    }
    if (hwloc_topology_load (*tp) < 0)
        goto error;
    return 0;
error:
    hwloc_topology_destroy (*tp);
    return -1;
}

static int topo_restrict (hwloc_topology_t topo)
{
    // this is not supported on macos, and actually flat fails
#ifdef __APPLE__
    return 0;
#else
    hwloc_bitmap_t set = NULL;
    int rc = -1;
    if (!(set = hwloc_bitmap_alloc ())
        || hwloc_get_cpubind (topo, set, HWLOC_CPUBIND_PROCESS) < 0
        || hwloc_topology_restrict (topo, set, 0) < 0)
        goto err;
    rc = 0;
err:
    hwloc_bitmap_free (set);
    return rc;
#endif
}

hwloc_topology_t rhwloc_xml_topology_load (const char *xml,
                                           rhwloc_flags_t in_flags)
{
    hwloc_topology_t topo = NULL;
    int flags = 0;
    if (!(in_flags & RHWLOC_NO_RESTRICT))
        flags |= HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM;
    if (init_topo_from_xml (&topo, xml, flags) < 0)
        return NULL;
    if (!(in_flags & RHWLOC_NO_RESTRICT)
        && topo_restrict (topo) < 0) {
        hwloc_topology_destroy (topo);
        return NULL;
    }
    return topo;
}

static char *topo_xml_export (hwloc_topology_t topo)
{
    char *buf = NULL;
    int buflen;
    char *result = NULL;

    if (!topo)
        return NULL;

#if HWLOC_API_VERSION >= 0x20000
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen, 0) < 0) {
#else
    if (hwloc_topology_export_xmlbuffer (topo, &buf, &buflen) < 0) {
#endif
        goto out;
    }
    result = strdup (buf);
out:
    if (buf)
        hwloc_free_xmlbuffer (topo, buf);
    return result;
}

/*  Restrict an XML topology by loading it with no flags (which automatically
 *  restricts to current resource binding), then re-export to XML:
 */
char *rhwloc_topology_xml_restrict (const char *xml)
{
    char *result;
    hwloc_topology_t topo;

    if (!(topo = rhwloc_xml_topology_load (xml, 0)))
        return NULL;
    result = topo_xml_export (topo);
    hwloc_topology_destroy (topo);
    return result;
}

hwloc_topology_t rhwloc_xml_topology_load_file (const char *path,
                                                rhwloc_flags_t flags)
{
    hwloc_topology_t topo;
    int fd;
    int rc;
    char *buf;
    if ((fd = open (path, O_RDONLY)) < 0)
        return NULL;
    rc = read_all (fd, (void **) &buf);
    ERRNO_SAFE_WRAP (close, fd);
    if (rc < 0)
        return NULL;
    /*  Load hwloc from XML file, add current system information from uname(2)
     *  unless already set.
     */
    if ((topo = rhwloc_xml_topology_load (buf, flags))
        && !rhwloc_hostname (topo)) {
        struct utsname utsname;
        int depth = hwloc_get_type_depth (topo, HWLOC_OBJ_MACHINE);
        hwloc_obj_t obj = hwloc_get_obj_by_depth (topo, depth, 0);

        /* Best effort */
        if (uname (&utsname) == 0) {
            hwloc_obj_add_info(obj, "OSName", utsname.sysname);
            hwloc_obj_add_info(obj, "OSRelease", utsname.release);
            hwloc_obj_add_info(obj, "OSVersion", utsname.version);
            hwloc_obj_add_info(obj, "HostName", utsname.nodename);
            hwloc_obj_add_info(obj, "Architecture", utsname.machine);
        }
    }
    ERRNO_SAFE_WRAP (free, buf);
    return topo;
}

hwloc_topology_t rhwloc_local_topology_load (rhwloc_flags_t flags)
{
    const char *xml;
    hwloc_topology_t topo = NULL;
    uint32_t hwloc_version = hwloc_get_api_version ();

    if ((hwloc_version >> 16) != (HWLOC_API_VERSION >> 16))
        return NULL;

    /*  Allow FLUX_HWLOC_XMLFILE to force loading topology from a file
     *  instead of the system. This is meant for testing usage only.
     *  If loading from the XML file fails for any reason, fall back
     *  to normal topology load.
     */
    if ((xml = getenv ("FLUX_HWLOC_XMLFILE"))
        && (topo = rhwloc_xml_topology_load_file (xml, flags)))
        return topo;

    if (topo_init_common (&topo, 0) < 0)
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
    if (flags & RHWLOC_NO_RESTRICT)
        return (topo);
    if (topo_restrict (topo) < 0)
        goto err;
    return (topo);
err:
    hwloc_topology_destroy (topo);
    return NULL;
}

char *rhwloc_local_topology_xml (rhwloc_flags_t rflags)
{
    char *result;
    hwloc_topology_t topo = rhwloc_local_topology_load (rflags);
    result = topo_xml_export (topo);
    hwloc_topology_destroy (topo);
    return result;
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
static bool backend_is_coproc (const char *s, const char *nvidia_backend)
{
    /* Only count cudaX or nvmlX, openclX, and rmsiX devices for now */
    return (streq (s, nvidia_backend)
            || streq (s, "OpenCL")
            || streq (s, "RSMI"));
}

char * rhwloc_gpu_idset_string (hwloc_topology_t topo)
{
    int index;
    char *result = NULL;
    hwloc_obj_t obj = NULL;
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);

    if (!ids)
        return NULL;

    /*  NVIDIA GPUs can be found by both the CUDA or NVML Backends.
     *  We would like to catch either option, but not double count
     *  if both are present, so make a first pass to see if any CUDA
     *  osdevs are present, otherwise check NVML in the next loop.
     */
    bool isCudaPresent = false;
    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && streq (s, "CUDA"))
            isCudaPresent = true;
    }

    /*  Manually index GPUs -- os_index does not seem to be valid for
     *  these devices in some cases, and logical index also seems
     *  incorrect (?)
     */
    index = 0;
    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && backend_is_coproc (s, isCudaPresent ? "CUDA" : "NVML"))
            idset_set (ids, index++);
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
    idset_destroy (ids);
    return result;
}

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
        topo = rhwloc_xml_topology_load (xml, RHWLOC_NO_RESTRICT);
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
    if (topo)
        hwloc_topology_destroy (topo);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
