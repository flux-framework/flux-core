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

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/utsname.h>

#include <flux/idset.h>
#include <jansson.h>

#include "ccan/str/str.h"
#include "src/common/libutil/read_all.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"

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
    if ((xml = getenv ("FLUX_HWLOC_XMLFILE"))) {
        int xml_flags = flags;

        /* If FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM is set, use RHWLOC_NO_RESTRICT
         * to skip both hwloc_topology_restrict() and the setting of
         * HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM, neither of which is appropriate
         * for a topology loaded from a different system.
         */
        if (getenv ("FLUX_HWLOC_XMLFILE_NOT_THISSYSTEM"))
            xml_flags |= RHWLOC_NO_RESTRICT;

        /*  If load is successful, return topo immediately so no further
         *  processing is done.
         */
        if ((topo = rhwloc_xml_topology_load_file (xml, xml_flags)))
            return topo;
    }

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
    static char hostname[_POSIX_HOST_NAME_MAX + 1];
    const char *name;
    int depth = hwloc_get_type_depth (topo, HWLOC_OBJ_MACHINE);
    hwloc_obj_t obj = hwloc_get_obj_by_depth (topo, depth, 0);

    if (obj && (name = hwloc_obj_get_info_by_name (obj, "HostName")))
        return name;
    /* Fall back to local hostname if HostName not available
     */
    if (hostname[0] == '\0') {
        if (gethostname (hostname, sizeof (hostname)) < 0)
            return NULL;
        hostname[sizeof (hostname) - 1] = '\0'; // POSIX doesn't guarantee NUL
    }
    return hostname;
}

/*  Return the union of cpusets for the cores in idset string `cores`.
 *  Returns heap-allocated hwloc_cpuset_t, or NULL on error.
 *  Caller must free with hwloc_bitmap_free().
 */
hwloc_cpuset_t rhwloc_cores_to_cpuset (hwloc_topology_t topo,
                                       const char *cores,
                                       flux_error_t *errp)
{
    hwloc_cpuset_t coreset = NULL;
    hwloc_cpuset_t cpuset = NULL;
    int depth;
    int i;

    if (!topo || !cores) {
        errprintf (errp, "Invalid argument");
        errno = EINVAL;
        return NULL;
    }

    if (!(coreset = hwloc_bitmap_alloc ())
        || !(cpuset = hwloc_bitmap_alloc ())) {
        errprintf (errp, "Error allocating hwloc bitmaps");
        errno = ENOMEM;
        goto err;
    }

    if (hwloc_bitmap_list_sscanf (coreset, cores) < 0) {
        errprintf (errp, "invalid core ID string: %s", cores);
        errno = EINVAL;
        goto err;
    }

    depth = hwloc_get_type_depth (topo, HWLOC_OBJ_CORE);
    if (depth == HWLOC_TYPE_DEPTH_UNKNOWN
        || depth == HWLOC_TYPE_DEPTH_MULTIPLE) {
        errprintf (errp, "hwloc reports invalid depth for Core objects");
        errno = EINVAL;
        goto err;
    }

    i = hwloc_bitmap_first (coreset);
    while (i >= 0) {
        hwloc_obj_t core = hwloc_get_obj_by_depth (topo, depth, i);
        if (!core || !core->cpuset) {
            errprintf (errp, "core %d not found in node topology", i);
            errno = ENOENT;
            goto err;
        }
        hwloc_bitmap_or (cpuset, cpuset, core->cpuset);
        i = hwloc_bitmap_next (coreset, i);
    }
    hwloc_bitmap_free (coreset);
    return cpuset;
err:
    hwloc_bitmap_free (coreset);
    hwloc_bitmap_free (cpuset);
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

/*  Walk up from obj to the nearest HWLOC_OBJ_PCI_DEVICE ancestor, or NULL.
 */
hwloc_obj_t rhwloc_osdev_get_pcidev (hwloc_obj_t obj)
{
    hwloc_obj_t p = obj->parent;
    while (p && p->type != HWLOC_OBJ_PCI_DEVICE)
        p = p->parent;
    return p;
}

/*  Return true if the hwloc "backend" type string matches a GPU
 *  which should be indexed as a compute GPU.
 */
static bool backend_is_coproc (const char *s)
{
    if (s == NULL)
        return false;
    return (streq (s, "CUDA")
            || streq (s, "NVML")
            || streq (s, "OpenCL")
            || streq (s, "RSMI"));
}

/*  Structure to track unique GPU identities by PCI device and backend.
 *  Deduplicates GPUs appearing under multiple backends (e.g., CUDA and
 *  OpenCL for the same physical NVIDIA GPU), while preserving AMD CPX/TPX
 *  partitioned GPUs which share a PCI device but are distinct logical GPUs.
 */
struct gpu_identity {
    hwloc_obj_t pcidev;
    const char *backend;
};

static bool gpu_identity_visited (struct gpu_identity *visited,
                                  int nvisited,
                                  hwloc_obj_t pcidev,
                                  const char *backend)
{
    if (pcidev && backend) {
        for (int i = 0; i < nvisited; i++) {
            if (visited[i].pcidev == pcidev
                && !streq (visited[i].backend, backend))
                return true;
        }
    }
    return false;
}

/*  Traverse topology osdevs and accumulate unique compute GPUs.
 *  visited[vlen] is a caller-provided zeroed work array; vlen bounds
 *  visited writes. If result != NULL, unique GPU osdev objects are stored
 *  there up to rlen entries. Returns the count of unique GPUs.
 */
static int collect_unique_gpus (hwloc_topology_t topo,
                                struct gpu_identity *visited,
                                int vlen,
                                hwloc_obj_t *result,
                                int rlen)
{
    int nvisited = 0;
    int count = 0;
    bool dedup = true;
    hwloc_obj_t obj = NULL;

    /*  Allow disabling deduplication via environment variable as an escape
     *  hatch in case this logic causes problems in the field.
     */
    if (getenv ("FLUX_HWLOC_GPU_NO_DEDUP"))
        dedup = false;

    /*  Manually index GPUs -- os_index does not seem to be valid for
     *  these devices in some cases, and logical index also seems
     *  incorrect (?).
     *  Deduplicate GPUs that appear with multiple backends (e.g., CUDA
     *  and OpenCL for the same NVIDIA GPU), while preserving AMD partitioned
     *  GPUs (CPX/TPX) which share a PCI device but have the same backend.
     */
    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *backend = hwloc_obj_get_info_by_name (obj, "Backend");
        hwloc_obj_t pcidev = rhwloc_osdev_get_pcidev (obj);
        if (!backend_is_coproc (backend)
            || (dedup && gpu_identity_visited (visited,
                                               nvisited,
                                               pcidev,
                                               backend)))
            continue;
        if (nvisited < vlen) {
            visited[nvisited].pcidev = pcidev;
            visited[nvisited].backend = backend;
            nvisited++;
        }
        if (result && count < rlen)
            result[count] = obj;
        count++;
    }
    return count;
}

hwloc_obj_t *rhwloc_gpu_objects (hwloc_topology_t topo, int *count_out)
{
    int n_pci;
    int count = 0;
    struct gpu_identity *visited = NULL;
    hwloc_obj_t *result = NULL;

    *count_out = 0;

    /* Get total count of PCI devices to size the visited array:
     */
    n_pci = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_PCI_DEVICE);
    if (n_pci <= 0) {
        errno = ENODEV;
        return NULL;
    }
    if (!(visited = calloc (n_pci, sizeof (*visited))))
        return NULL;

    /* Allocate result for worst case (all PCI devices are GPUs), then
     * traverse once to collect unique GPUs
     */
    if (!(result = calloc (n_pci, sizeof (*result))))
        goto out;
    count = collect_unique_gpus (topo, visited, n_pci, result, n_pci);
    if (count == 0) {
        free (result);
        result = NULL;
        goto out;
    }
    *count_out = count;
out:
    ERRNO_SAFE_WRAP (free, visited);
    return result;
}

static struct idset *rhwloc_gpu_idset (hwloc_topology_t topo)
{
    int n_pci;
    int count = 0;
    struct gpu_identity *visited = NULL;
    struct idset *ids = NULL;

    /* Count total PCI objects to size visited array:
     */
    n_pci = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_PCI_DEVICE);
    if (n_pci <= 0 || !(visited = calloc (n_pci, sizeof (*visited))))
        return NULL;

    /* Count unique GPUs (no need to collect actual objects):
     */
    count = collect_unique_gpus (topo, visited, n_pci, NULL, 0);
    if (count == 0
        || !(ids = idset_create (0, IDSET_FLAG_AUTOGROW))
        || idset_range_set (ids, 0, count - 1) < 0) {
        idset_destroy (ids);
        ids = NULL;
    }

    ERRNO_SAFE_WRAP (free, visited);
    return ids;
}

char *rhwloc_gpu_idset_string (hwloc_topology_t topo)
{
    struct idset *ids;
    char *result = NULL;

    if (!(ids = rhwloc_gpu_idset (topo)))
        return NULL;
    result = idset_encode (ids, IDSET_FLAG_RANGE);
    idset_destroy (ids);
    return result;
}

int rhwloc_count_type (hwloc_topology_t topo, const char *name)
{
    hwloc_obj_type_t type;
    int count;

    if (streq (name, "gpu")) {
        /* Handle GPU case manually since hwloc doesn't support it
         */
        struct idset *ids = rhwloc_gpu_idset (topo);
        if (!ids)
            return -1;
        count = idset_count (ids);
        idset_destroy (ids);
        return count;
    }

    if (hwloc_type_sscanf (name, &type, NULL, 0) < 0
        || (count = hwloc_get_nbobjs_by_type (topo, type)) < 0) {
        errno = EINVAL;
        return -1;
    }
    return count;
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
