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

#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"

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
                      flux_error_t *errp,
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
        errprintf (errp, "Invalid argument");
        errno = EINVAL;
        return -1;
    }
    if (!(cpuset = rhwloc_cores_to_cpuset (m->topo, cores, errp)))
        goto out;
    if (!(nodeset = hwloc_bitmap_alloc ())) {
        errprintf (errp, "Out of memory");
        errno = ENOMEM;
        goto out;
    }
    if (hwloc_cpuset_to_nodeset (m->topo, cpuset, nodeset) != 0) {
        errprintf (errp,
                   "failed to get NUMA nodes for cores: %s",
                   cores);
        goto out;
    }
    if (!(cpus = bitmap_to_idset (cpuset))
        || !(mems = bitmap_to_idset (nodeset))) {
        errprintf (errp, "out of memory");
        goto out;
    }
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

int rhwloc_map_count_type (rhwloc_map_t *m, const char *type)
{
    if (!m || !type) {
        errno = EINVAL;
        return -1;
    }
    return rhwloc_count_type (m->topo, type);
}

/* Return the PCI address of the GPU osdev object's nearest PCI ancestor
 * as a heap-allocated string, or NULL if no PCI ancestor is found.
 */
static char *gpu_pci_addr (hwloc_obj_t obj, flux_error_t *errp)
{
    hwloc_obj_t parent = obj->parent;
    char buf[16]; /* "0000:ff:1f.7" + NUL */
    char *s;

    while (parent && parent->type != HWLOC_OBJ_PCI_DEVICE)
        parent = parent->parent;
    if (!parent) {
        errno = ENODEV;
        errprintf (errp,
                   "Failed to find PCI ancestor of GPU (logical id=%d)",
                   obj->logical_index);
        return NULL;
    }
    snprintf (buf, sizeof (buf), "%04x:%02x:%02x.%x",
              parent->attr->pcidev.domain,
              parent->attr->pcidev.bus,
              parent->attr->pcidev.dev,
              parent->attr->pcidev.func);
    if (!(s = strdup (buf)))
        errprintf (errp, "Out of memory copying GPU PCI addr");
    return s;
}

char **rhwloc_map_gpu_pci_addrs (rhwloc_map_t *m,
                                 const char *gpus,
                                 flux_error_t *errp)
{
    struct idset *gpuset = NULL;
    hwloc_obj_t *gpu_objs = NULL;
    char **result = NULL;
    int gpu_count = 0;

    if (!m || !gpus) {
        errprintf (errp, "Invalid argument");
        errno = EINVAL;
        return NULL;
    }
    if (!(gpuset = idset_decode (gpus))) {
        errprintf (errp, "idset_decode(\"%s\") failed", gpus);
        goto error;
    }
    if (!(result = calloc (idset_count (gpuset) + 1, sizeof (*result)))) {
        errprintf (errp, "Out of memory");
        goto error;
    }
    errno = 0;
    if (!(gpu_objs = rhwloc_gpu_objects (m->topo, &gpu_count))
        && gpu_count > 0) {
        errprintf (errp,
                   "Failed to collect GPU objects from hwloc: %s",
                   strerror (errno));
        goto error;
    }

    int i = 0;
    unsigned int id = idset_first (gpuset);
    while (id != IDSET_INVALID_ID) {
        if ((int)id >= gpu_count) {
            errprintf (errp,
                       "gpu%d not found in local topology (gpu count=%d)",
                        id,
                        gpu_count);
            goto error;
        }
        if (!(result[i++] = gpu_pci_addr (gpu_objs[id], errp)))
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
