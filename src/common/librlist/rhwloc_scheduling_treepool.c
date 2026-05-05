/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rhwloc_scheduling_treepool.c - TreePool topo object from hwloc */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <flux/idset.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"
#include "ccan/array_size/array_size.h"
#include "rhwloc.h"

/* Maps hwloc object types to TreePool scheduling properties.
 * Names come from RFC 49; types absent from that spec use NULL here and
 * fall back to hwloc_obj_type_string() so new hwloc types are picked up
 * automatically without a code change.
 */
struct obj_class {
    hwloc_obj_type_t type;
    bool is_cpu_container;
    const char *topo_name;  /* RFC 49 name, or NULL for hwloc_obj_type_string() */
};

static const struct obj_class obj_classes[] = {
    { HWLOC_OBJ_PACKAGE,  true,  "socket" },  /* RFC 49 */
    { HWLOC_OBJ_DIE,      true,  NULL     },
    { HWLOC_OBJ_GROUP,    true,  NULL     },
    { HWLOC_OBJ_NUMANODE, false, "numa"   },  /* RFC 49 */
};

static const struct obj_class *obj_class_get (hwloc_obj_type_t type)
{
    for (size_t i = 0; i < ARRAY_SIZE (obj_classes); i++)
        if (obj_classes[i].type == type)
            return &obj_classes[i];
    return NULL;
}

/* Return true if obj is a scheduling-relevant CPU-side container. */
static bool object_is_cpu_container (hwloc_obj_t obj)
{
    if (!obj || !obj->cpuset)
        return false;
    const struct obj_class *cls = obj_class_get (obj->type);
    return cls && cls->is_cpu_container;
}

/* Return the RFC 49 topo key for obj, falling back to the hwloc type
 * string for types not defined in the spec. */
static const char *object_topo_name (hwloc_obj_t obj)
{
    const struct obj_class *cls = obj_class_get (obj->type);
    if (cls && cls->topo_name)
        return cls->topo_name;
    return hwloc_obj_type_string (obj->type);
}

/* Return true if 'ancestor' appears anywhere in obj's parent chain. */
static bool obj_is_under (hwloc_obj_t obj, hwloc_obj_t ancestor)
{
    for (hwloc_obj_t p = obj->parent; p != NULL; p = p->parent)
        if (p == ancestor)
            return true;
    return false;
}

/* Return idset string of logical core indices whose cpuset falls within
 * 'cpuset'.  Returns NULL if none found.  Caller frees.
 */
static char *cpuset_cores_idset (hwloc_topology_t topo, hwloc_bitmap_t cpuset)
{
    struct idset *ids = idset_create (0, IDSET_FLAG_AUTOGROW);
    char *result = NULL;
    int depth;
    int n;

    if (!ids)
        return NULL;
    depth = hwloc_get_type_depth (topo, HWLOC_OBJ_CORE);
    if (depth < 0)
        goto out;
    n = hwloc_get_nbobjs_by_depth (topo, depth);
    for (int i = 0; i < n; i++) {
        hwloc_obj_t core = hwloc_get_obj_by_depth (topo, depth, i);
        if (core && core->cpuset
            && hwloc_bitmap_isincluded (core->cpuset, cpuset))
            idset_set (ids, core->logical_index);
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

/* Return true if GPU (OSDev) belongs to topology object 'obj'.
 * Tries tree walk first (works in hwloc 1.x where NUMANode/Package appear
 * in the PCIDev parent chain), then falls back to cpuset containment (hwloc
 * 2.x attaches I/O objects to their closest normal CPU ancestor, so NUMANode
 * is not in the PCIDev parent chain but Package is).
 */
static bool gpu_belongs_to_obj (hwloc_obj_t gpu, hwloc_obj_t obj)
{
    hwloc_obj_t pcidev = rhwloc_osdev_get_pcidev (gpu);
    if (!pcidev)
        return false;
    if (obj_is_under (pcidev, obj))
        return true;
    if (!obj->cpuset)
        return false;
    hwloc_obj_t anc = pcidev->parent;
    while (anc && !anc->cpuset)
        anc = anc->parent;
    return anc && hwloc_bitmap_isincluded (anc->cpuset, obj->cpuset);
}

/* Return idset string of GPU indices belonging to 'obj', or NULL if none.
 * Indices placed are also recorded in 'emitted' if non-NULL.
 */
static char *gpus_idset_for_obj (hwloc_obj_t obj,
                                  hwloc_obj_t *gpus, int ngpus,
                                  struct idset *emitted)
{
    struct idset *ids;
    char *result = NULL;

    if (!gpus || ngpus <= 0)
        return NULL;
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    for (int i = 0; i < ngpus; i++) {
        if (gpu_belongs_to_obj (gpus[i], obj)) {
            idset_set (ids, i);
            if (emitted)
                idset_set (emitted, i);
        }
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
    idset_destroy (ids);
    return result;
}

/* Return idset string of GPU indices belonging to 'obj' that have NOT yet
 * been emitted (i.e., not in 'emitted').  Records newly emitted indices in
 * 'emitted'.  Returns NULL if none.  Caller frees.
 */
static char *residual_gpus_for_obj (hwloc_obj_t obj,
                                     hwloc_obj_t *gpus, int ngpus,
                                     struct idset *emitted)
{
    struct idset *ids;
    char *result = NULL;

    if (!gpus || ngpus <= 0 || !emitted)
        return NULL;
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    for (int i = 0; i < ngpus; i++) {
        if (!idset_test (emitted, i) && gpu_belongs_to_obj (gpus[i], obj)) {
            idset_set (ids, i);
            idset_set (emitted, i);
        }
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
    idset_destroy (ids);
    return result;
}

/* Collect NUMA nodes whose cpuset is included in 'cpuset'.
 * Returns count; if numas non-NULL stores up to max pointers there.
 */
static int collect_numa_for_cpuset (hwloc_topology_t topo,
                                     hwloc_bitmap_t cpuset,
                                     hwloc_obj_t *numas,
                                     int max)
{
    int n = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_NUMANODE);
    int count = 0;

    for (int i = 0; i < n; i++) {
        hwloc_obj_t numa = hwloc_get_obj_by_type (topo, HWLOC_OBJ_NUMANODE, i);
        if (!numa || !numa->cpuset || hwloc_bitmap_iszero (numa->cpuset))
            continue;
        if (!hwloc_bitmap_isincluded (numa->cpuset, cpuset))
            continue;
        if (numas && count < max)
            numas[count] = numa;
        count++;
    }
    return count;
}

/* Find CPU containers that are direct (or indirect-through-boring) normal
 * children of 'parent' whose cpuset is a subset of 'scope'.  Descends
 * through non-container, non-Core/PU objects (e.g. L3Cache).
 * Returns count; stores up to max pointers in out[] if non-NULL.
 */
static int find_cpu_containers (hwloc_obj_t parent,
                                 hwloc_bitmap_t scope,
                                 hwloc_obj_t *out, int max)
{
    int n_direct = 0;

    for (unsigned i = 0; i < parent->arity; i++) {
        hwloc_obj_t child = parent->children[i];
        if (!child || !child->cpuset || hwloc_bitmap_iszero (child->cpuset))
            continue;
        if (!hwloc_bitmap_isincluded (child->cpuset, scope))
            continue;
        if (object_is_cpu_container (child))
            n_direct++;
    }
    if (n_direct > 0) {
        int count = 0;
        for (unsigned i = 0; i < parent->arity; i++) {
            hwloc_obj_t child = parent->children[i];
            if (!child || !child->cpuset || hwloc_bitmap_iszero (child->cpuset))
                continue;
            if (!hwloc_bitmap_isincluded (child->cpuset, scope))
                continue;
            if (object_is_cpu_container (child)) {
                if (out && count < max)
                    out[count] = child;
                count++;
            }
        }
        return count;
    }
    /* No containers at this level: descend through boring intermediates. */
    int total = 0;
    for (unsigned i = 0; i < parent->arity; i++) {
        hwloc_obj_t child = parent->children[i];
        if (!child || !child->cpuset || hwloc_bitmap_iszero (child->cpuset))
            continue;
        if (!hwloc_bitmap_isincluded (child->cpuset, scope))
            continue;
        if (child->type == HWLOC_OBJ_CORE || child->type == HWLOC_OBJ_PU)
            continue;
        total += find_cpu_containers (child, scope,
                                       out ? out + total : NULL,
                                       out ? max - total : 0);
    }
    return total;
}

/* Build a leaf JSON entry from a cpuset with no NUMA children.
 * Emits cores and any GPUs belonging to obj; no memory (caller's fallback).
 */
static json_t *cpuset_leaf_json (hwloc_topology_t topo,
                                  hwloc_obj_t obj,
                                  hwloc_obj_t *gpus, int ngpus,
                                  struct idset *emitted_gpus,
                                  flux_error_t *errp)
{
    char *cores = cpuset_cores_idset (topo, obj->cpuset);
    char *gpu_ids = gpus_idset_for_obj (obj, gpus, ngpus, emitted_gpus);
    json_t *o = NULL;

    if (!cores) {
        errprintf (errp, "%s[%u]: no Core objects found in cpuset",
                   hwloc_obj_type_string (obj->type), obj->logical_index);
        goto out;
    }
    if (!(o = json_object ())
        || json_object_set_new (o, "cores", json_string (cores)) < 0)
        goto err;
    if (gpu_ids
        && json_object_set_new (o, "gpus", json_string (gpu_ids)) < 0)
        goto err;
    goto out;
err:
    json_decref (o);
    o = NULL;
out:
    free (cores);
    free (gpu_ids);
    return o;
}

/* Build a leaf JSON entry from a NUMANode.
 * Emits cores, GPUs, and local memory.  Sets *found_mem if memory > 0.
 */
static json_t *numa_leaf_json (hwloc_topology_t topo,
                                hwloc_obj_t numa,
                                hwloc_obj_t *gpus, int ngpus,
                                bool *found_mem,
                                struct idset *emitted_gpus,
                                flux_error_t *errp)
{
    char *cores = cpuset_cores_idset (topo, numa->cpuset);
    char *gpu_ids = gpus_idset_for_obj (numa, gpus, ngpus, emitted_gpus);
    uint64_t mem_gib = numa->attr->numanode.local_memory
                       / (1024ULL * 1024 * 1024);
    json_t *o = NULL;

    if (!cores) {
        errprintf (errp, "NUMANode[%u]: no Core objects found in cpuset",
                   numa->logical_index);
        goto out;
    }
    if (!(o = json_object ())
        || json_object_set_new (o, "cores", json_string (cores)) < 0)
        goto err;
    if (gpu_ids
        && json_object_set_new (o, "gpus", json_string (gpu_ids)) < 0)
        goto err;
    if (mem_gib > 0) {
        if (json_object_set_new (o, "memory",
                                  json_integer ((json_int_t)mem_gib)) < 0)
            goto err;
        if (found_mem)
            *found_mem = true;
    }
    goto out;
err:
    json_decref (o);
    o = NULL;
out:
    free (cores);
    free (gpu_ids);
    return o;
}

/* Recursively build a topo object for the scope defined by hwloc obj.
 *
 * Traversal order is determined by the actual tree:
 *   1. If CPU containers (Package, Die, Group, …) exist as children of obj,
 *      group them under their type name.  A single container is folded.
 *   2. Otherwise, if NUMA nodes fall within obj's cpuset, group them under
 *      "numa".  A single NUMA is folded into a leaf.
 *   3. Otherwise emit a leaf (cores + GPUs) directly.
 *
 * After processing children, any GPUs that belong to obj but were not
 * claimed by a child are attached at this scope level.
 *
 * GPU indices emitted at any level are recorded in emitted_gpus.
 * found_mem is set to true if memory is emitted at any level.
 */
static json_t *build_topo_obj (hwloc_topology_t topo,
                                hwloc_obj_t obj,
                                hwloc_obj_t *gpus, int ngpus,
                                bool *found_mem,
                                struct idset *emitted_gpus,
                                flux_error_t *errp)
{
    int ncontainers = find_cpu_containers (obj, obj->cpuset, NULL, 0);
    json_t *result = NULL;

    if (ncontainers > 0) {
        hwloc_obj_t *containers = calloc (ncontainers, sizeof (*containers));
        if (!containers)
            return NULL;
        find_cpu_containers (obj, obj->cpuset, containers, ncontainers);

        if (ncontainers == 1) {
            /* Single container: fold without adding a wrapper level. */
            result = build_topo_obj (topo, containers[0], gpus, ngpus,
                                      found_mem, emitted_gpus, errp);
        } else {
            /* Determine name from first container, but look through
             * transparent Group wrappers (e.g. hwloc 2.x inserting a
             * Group around each NUMANode→Package from 1.x XML) to find
             * the underlying hardware boundary name (e.g. "socket"). */
            const char *name = object_topo_name (containers[0]);
            if (containers[0]->type == HWLOC_OBJ_GROUP) {
                hwloc_obj_t probe = containers[0];
                while (probe->type == HWLOC_OBJ_GROUP) {
                    hwloc_obj_t inner;
                    if (find_cpu_containers (probe, probe->cpuset,
                                             &inner, 1) != 1)
                        break;
                    probe = inner;
                }
                if (probe != containers[0])
                    name = object_topo_name (probe);
            }
            json_t *arr = json_array ();
            if (!arr) { free (containers); return NULL; }
            for (int i = 0; i < ncontainers; i++) {
                json_t *child = build_topo_obj (topo, containers[i], gpus, ngpus,
                                                 found_mem, emitted_gpus, errp);
                if (!child || json_array_append_new (arr, child) < 0) {
                    json_decref (arr);
                    free (containers);
                    return NULL;
                }
            }
            if (!(result = json_object ())
                || json_object_set_new (result, name, arr) < 0) {
                json_decref (result);
                json_decref (arr);
                result = NULL;
            }
        }
        free (containers);

        /* GPUs that belong to this scope but weren't claimed by any child. */
        if (result) {
            char *gpu_ids = residual_gpus_for_obj (obj, gpus, ngpus, emitted_gpus);
            if (gpu_ids)
                (void) json_object_set_new (result, "gpus", json_string (gpu_ids));
            free (gpu_ids);
        }
        return result;
    }

    /* No CPU containers: fall through to NUMA or leaf. */
    int nnuma = collect_numa_for_cpuset (topo, obj->cpuset, NULL, 0);

    if (nnuma == 0)
        return cpuset_leaf_json (topo, obj, gpus, ngpus, emitted_gpus, errp);

    hwloc_obj_t *numas = calloc (nnuma, sizeof (*numas));
    if (!numas)
        return NULL;
    collect_numa_for_cpuset (topo, obj->cpuset, numas, nnuma);

    if (nnuma == 1) {
        /* Single NUMA: fold into leaf. */
        result = numa_leaf_json (topo, numas[0], gpus, ngpus,
                                  found_mem, emitted_gpus, errp);
    } else {
        json_t *numa_arr = json_array ();
        if (!numa_arr) { free (numas); return NULL; }
        for (int i = 0; i < nnuma; i++) {
            json_t *leaf = numa_leaf_json (topo, numas[i], gpus, ngpus,
                                           found_mem, emitted_gpus, errp);
            if (!leaf || json_array_append_new (numa_arr, leaf) < 0) {
                json_decref (numa_arr);
                free (numas);
                return NULL;
            }
        }
        if (!(result = json_object ())
            || json_object_set_new (result, "numa", numa_arr) < 0) {
            json_decref (result);
            json_decref (numa_arr);
            result = NULL;
        }
        /* GPUs belonging to this scope but not placed by any NUMA child
         * (e.g. hwloc 2.x SNC where I/O objects attach to Package). */
        if (result) {
            char *gpu_ids = residual_gpus_for_obj (obj, gpus, ngpus, emitted_gpus);
            if (gpu_ids)
                (void) json_object_set_new (result, "gpus", json_string (gpu_ids));
            free (gpu_ids);
        }
    }
    free (numas);
    return result;
}

json_t *rhwloc_scheduling_treepool (hwloc_topology_t topo, flux_error_t *errp)
{
    hwloc_obj_t machine = hwloc_get_root_obj (topo);
    hwloc_obj_t *gpus = NULL;
    int ngpus = 0;
    json_t *node_obj = NULL;
    json_t *result = NULL;
    bool found_mem = false;
    uint64_t total_machine_mem = 0;
    struct idset *emitted_gpus = NULL;

    if (!machine || !machine->cpuset)
        return NULL;

    gpus = rhwloc_gpu_objects (topo, &ngpus);

    int nnuma_total = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_NUMANODE);
    for (int i = 0; i < nnuma_total; i++) {
        hwloc_obj_t n = hwloc_get_obj_by_type (topo, HWLOC_OBJ_NUMANODE, i);
        if (n)
            total_machine_mem += n->attr->numanode.local_memory;
    }

    if (!(emitted_gpus = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto out;

    node_obj = build_topo_obj (topo, machine, gpus, ngpus,
                                &found_mem, emitted_gpus, errp);
    if (!node_obj)
        goto out;

    /* Memory fallback: attach machine total at node scope when no lower
     * level reported memory (e.g. NUMA node spans multiple packages). */
    if (!found_mem && total_machine_mem > 0) {
        uint64_t mem_gib = total_machine_mem / (1024ULL * 1024 * 1024);
        if (mem_gib > 0)
            (void) json_object_set_new (node_obj, "memory",
                                        json_integer ((json_int_t)mem_gib));
    }

    /* GPU fallback: any GPU not placed at any scope level goes to node. */
    if (ngpus > 0) {
        struct idset *node_ids = idset_create (0, IDSET_FLAG_AUTOGROW);
        if (node_ids) {
            for (int g = 0; g < ngpus; g++) {
                if (!idset_test (emitted_gpus, g))
                    idset_set (node_ids, g);
            }
            if (idset_count (node_ids) > 0) {
                char *s = idset_encode (node_ids, IDSET_FLAG_RANGE);
                if (s)
                    (void) json_object_set_new (node_obj, "gpus",
                                                json_string (s));
                free (s);
            }
            idset_destroy (node_ids);
        }
    }

    result = node_obj;
    node_obj = NULL;
out:
    idset_destroy (emitted_gpus);
    json_decref (node_obj);
    free (gpus);
    return result;
}

/* vi: ts=4 sw=4 expandtab
 */
