/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
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

#include <stdlib.h>
#include <string.h>
#include <flux/idset.h>

#include "ccan/str/str.h"
#include "src/common/libutil/errno_safe.h"

#include "rhwloc.h"
#include "rhwloc_map.h"

struct rhwloc_map {
    hwloc_topology_t topo;
};

rhwloc_map_t *rhwloc_map_create (const char *xml)
{
    rhwloc_map_t *m;

    if (!(m = calloc (1, sizeof (*m))))
        return NULL;
    if (!(m->topo = rhwloc_xml_topology_load (xml, RHWLOC_NO_RESTRICT))) {
        free (m);
        return NULL;
    }
    return m;
}

void rhwloc_map_destroy (rhwloc_map_t *m)
{
    if (m) {
        int saved_errno = errno;
        hwloc_topology_destroy (m->topo);
        free (m);
        errno = saved_errno;
    }
}

void rhwloc_map_strv_free (char **strv)
{
    if (strv) {
        for (int i = 0; strv[i] != NULL; i++)
            free (strv[i]);
        free (strv);
    }
}

/* Convert an hwloc bitmap to a heap-allocated idset range string.
 */
static char *bitmap_to_idset (hwloc_bitmap_t bitmap)
{
    struct idset *ids;
    char *result = NULL;
    int i;

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    i = hwloc_bitmap_first (bitmap);
    while (i >= 0) {
        if (idset_set (ids, i) < 0)
            goto out;
        i = hwloc_bitmap_next (bitmap, i);
    }
    result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

int rhwloc_map_cores (rhwloc_map_t *m,
                      const char *cores,
                      char **cpus_out,
                      char **mems_out)
{
    hwloc_cpuset_t cpuset = NULL;
    hwloc_bitmap_t nodeset = NULL;
    char *cpus = NULL;
    char *mems = NULL;
    int rc = -1;

    if (!m || !cores || !cpus_out || !mems_out) {
        errno = EINVAL;
        return -1;
    }
    if (!(cpuset = rhwloc_cores_to_cpuset (m->topo, cores))
        || !(nodeset = hwloc_bitmap_alloc ())
        || hwloc_cpuset_to_nodeset (m->topo, cpuset, nodeset) != 0)
        goto out;
    if (!(cpus = bitmap_to_idset (cpuset))
        || !(mems = bitmap_to_idset (nodeset)))
        goto out;
    *cpus_out = cpus;
    *mems_out = mems;
    cpus = mems = NULL;
    rc = 0;
out:
    ERRNO_SAFE_WRAP (hwloc_bitmap_free, cpuset);
    ERRNO_SAFE_WRAP (hwloc_bitmap_free, nodeset);
    ERRNO_SAFE_WRAP (free, cpus);
    ERRNO_SAFE_WRAP (free, mems);
    return rc;
}

/* Return true if the hwloc backend string identifies a compute GPU.
 * Mirrors the logic in rhwloc_gpu_idset_string().
 */
static bool backend_is_coproc (const char *s, const char *nvidia_backend)
{
    return (streq (s, nvidia_backend)
            || streq (s, "OpenCL")
            || streq (s, "RSMI"));
}

/* Collect compute GPU osdev objects from the topology in hwloc order into a
 * heap-allocated array.  Returns the array and sets *count_out, or returns
 * NULL if no GPUs are found or on allocation failure.  Caller must free().
 */
static hwloc_obj_t *collect_gpu_objects (hwloc_topology_t topo, int *count_out)
{
    hwloc_obj_t obj = NULL;
    hwloc_obj_t *result;
    bool isCudaPresent = false;
    const char *nvidia_backend;
    int count = 0;

    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && streq (s, "CUDA")) {
            isCudaPresent = true;
            break;
        }
    }
    nvidia_backend = isCudaPresent ? "CUDA" : "NVML";

    obj = NULL;
    while ((obj = hwloc_get_next_osdev (topo, obj))) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && backend_is_coproc (s, nvidia_backend))
            count++;
    }
    if (count == 0) {
        *count_out = 0;
        return NULL;
    }
    if (!(result = calloc (count, sizeof (*result))))
        return NULL;
    obj = NULL;
    int i = 0;
    while ((obj = hwloc_get_next_osdev (topo, obj)) && i < count) {
        const char *s = hwloc_obj_get_info_by_name (obj, "Backend");
        if (s && backend_is_coproc (s, nvidia_backend))
            result[i++] = obj;
    }
    *count_out = count;
    return result;
}

/* Return the PCI address of the GPU osdev object's nearest PCI ancestor
 * as a heap-allocated string, or NULL if no PCI ancestor is found.
 */
static char *gpu_pci_addr (hwloc_obj_t obj)
{
    hwloc_obj_t parent = obj->parent;
    char buf[16]; /* "0000:ff:1f.7" + NUL */

    while (parent && parent->type != HWLOC_OBJ_PCI_DEVICE)
        parent = parent->parent;
    if (!parent)
        return NULL;
    snprintf (buf, sizeof (buf), "%04x:%02x:%02x.%x",
              parent->attr->pcidev.domain,
              parent->attr->pcidev.bus,
              parent->attr->pcidev.dev,
              parent->attr->pcidev.func);
    return strdup (buf);
}

char **rhwloc_map_gpu_pci_addrs (rhwloc_map_t *m, const char *gpus)
{
    struct idset *gpuset = NULL;
    hwloc_obj_t *gpu_objs = NULL;
    char **result = NULL;
    int gpu_count = 0;

    if (!m || !gpus) {
        errno = EINVAL;
        return NULL;
    }
    if (!(gpuset = idset_decode (gpus)))
        goto error;
    if (!(result = calloc (idset_count (gpuset) + 1, sizeof (*result))))
        goto error;
    if (!(gpu_objs = collect_gpu_objects (m->topo, &gpu_count))
        && gpu_count > 0)
        goto error;

    int i = 0;
    unsigned int id = idset_first (gpuset);
    while (id != IDSET_INVALID_ID) {
        if ((int)id >= gpu_count || !(result[i++] = gpu_pci_addr (gpu_objs[id])))
            goto error;
        id = idset_next (gpuset, id);
    }
    free (gpu_objs);
    idset_destroy (gpuset);
    return result;
error:
    rhwloc_map_strv_free (result);
    free (gpu_objs);
    idset_destroy (gpuset);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
