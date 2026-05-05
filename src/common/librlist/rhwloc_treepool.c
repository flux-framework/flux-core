/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* rhwloc_treepool.c - TreePool topo object from hwloc */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <flux/idset.h>
#include <jansson.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"
#include "ccan/array_size/array_size.h"
#include "rhwloc.h"
#include "rhwloc_treepool.h"

#define BYTES_PER_GIB (1024ULL * 1024 * 1024)

/* Context for building TreePool topology objects.
 * Bundles parameters that are threaded through all helper functions.
 */
struct topo_build_ctx {
    hwloc_topology_t topo;       /* hwloc topology being walked */
    hwloc_obj_t *gpus;           /* array of GPU objects */
    int ngpus;                   /* count of GPUs */
    struct idset *emitted_gpus;  /* tracks which GPUs have been assigned */
    bool found_mem;              /* set to true if any memory is emitted */
};

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

/* Common helper for building GPU idset strings.
 * If only_residual is true, only includes GPUs not already in 'emitted'.
 * Records placed GPU indices in 'emitted' if non-NULL.
 * Returns idset string or NULL if no GPUs match.  Caller frees.
 */
static char *build_gpu_idset (hwloc_obj_t obj,
                              hwloc_obj_t *gpus,
                              int ngpus,
                              struct idset *emitted,
                              bool only_residual)
{
    struct idset *ids;
    char *result = NULL;

    if (!gpus || ngpus <= 0)
        return NULL;
    if (only_residual && !emitted)
        return NULL;
    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        return NULL;
    for (int i = 0; i < ngpus; i++) {
        if (only_residual && idset_test (emitted, i))
            continue;
        if (gpu_belongs_to_obj (gpus[i], obj)) {
            if (idset_set (ids, i) < 0)
                goto out;
            if (emitted && idset_set (emitted, i) < 0)
                goto out;
        }
    }
    if (idset_count (ids) > 0)
        result = idset_encode (ids, IDSET_FLAG_RANGE);
out:
    idset_destroy (ids);
    return result;
}

/* Return idset string of GPU indices belonging to 'obj', or NULL if none.
 * Indices placed are also recorded in 'emitted' if non-NULL.
 */
static char *gpus_idset_for_obj (hwloc_obj_t obj,
                                 hwloc_obj_t *gpus,
                                 int ngpus,
                                 struct idset *emitted)
{
    return build_gpu_idset (obj, gpus, ngpus, emitted, false);
}

/* Return idset string of GPU indices belonging to 'obj' that have NOT yet
 * been emitted (i.e., not in 'emitted').  Records newly emitted indices in
 * 'emitted'.  Returns NULL if none.  Caller frees.
 */
static char *residual_gpus_for_obj (hwloc_obj_t obj,
                                    hwloc_obj_t *gpus,
                                    int ngpus,
                                    struct idset *emitted)
{
    return build_gpu_idset (obj, gpus, ngpus, emitted, true);
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
                                hwloc_obj_t *out,
                                int max)
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
        total += find_cpu_containers (child,
                                      scope,
                                      out ? out + total : NULL,
                                      out ? max - total : 0);
    }
    return total;
}

/* Build a leaf JSON entry from a cpuset with no NUMA children.
 * Emits cores and any GPUs belonging to obj; no memory (caller's fallback).
 * Sets errno on failure.
 */
static json_t *cpuset_leaf_json (struct topo_build_ctx *ctx,
                                 hwloc_obj_t obj,
                                 flux_error_t *errp)
{
    char *cores = rhwloc_core_idset_string (ctx->topo, obj->cpuset);
    char *gpu_ids = NULL;
    json_t *o = NULL;

    /* Validate cores before claiming GPUs: gpus_idset_for_obj() records the
     * GPUs it returns into ctx->emitted_gpus as a side effect, so claiming
     * them only after the leaf is known-good avoids consuming GPUs for a leaf
     * that is then discarded. */
    if (!cores) {
        errprintf (errp,
                   "%s[%u]: no Core objects found in cpuset",
                   hwloc_obj_type_string (obj->type),
                   obj->logical_index);
        errno = EINVAL;
        goto out;
    }
    gpu_ids = gpus_idset_for_obj (obj, ctx->gpus, ctx->ngpus, ctx->emitted_gpus);
    if (!(o = json_object ())
        || json_object_set_new (o, "cores", json_string (cores)) < 0) {
        errprintf (errp, "failed to create leaf JSON object");
        goto err;
    }
    if (gpu_ids
        && json_object_set_new (o, "gpus", json_string (gpu_ids)) < 0) {
        errprintf (errp, "failed to add gpus to leaf JSON object");
        goto err;
    }
    goto out;
err:
    ERRNO_SAFE_WRAP (json_decref, o);
    o = NULL;
out:
    ERRNO_SAFE_WRAP (free, cores);
    ERRNO_SAFE_WRAP (free, gpu_ids);
    return o;
}

/* Build a leaf JSON entry from a NUMANode.
 * Emits cores, GPUs, and local memory.  Sets ctx->found_mem if memory > 0.
 * Sets errno and errp on failure.
 */
static json_t *numa_leaf_json (struct topo_build_ctx *ctx,
                               hwloc_obj_t numa,
                               flux_error_t *errp)
{
    char *cores = rhwloc_core_idset_string (ctx->topo, numa->cpuset);
    char *gpu_ids = NULL;
    /* attr is guaranteed for a real NUMANODE, but guard anyway since the
     * topology may originate from untrusted XML. */
    uint64_t mem_gib = numa->attr
                       ? numa->attr->numanode.local_memory / BYTES_PER_GIB
                       : 0;
    json_t *o = NULL;

    /* Validate cores before claiming GPUs: gpus_idset_for_obj() records the
     * GPUs it returns into ctx->emitted_gpus as a side effect, so claiming
     * them only after the leaf is known-good avoids consuming GPUs for a leaf
     * that is then discarded. */
    if (!cores) {
        errprintf (errp,
                   "NUMANode[%u]: no Core objects found in cpuset",
                   numa->logical_index);
        errno = EINVAL;
        goto out;
    }
    gpu_ids = gpus_idset_for_obj (numa, ctx->gpus, ctx->ngpus, ctx->emitted_gpus);
    if (!(o = json_object ())
        || json_object_set_new (o, "cores", json_string (cores)) < 0) {
        errprintf (errp, "failed to create NUMA leaf JSON object");
        goto err;
    }
    if (gpu_ids
        && json_object_set_new (o, "gpus", json_string (gpu_ids)) < 0) {
        errprintf (errp, "failed to add gpus to NUMA leaf JSON object");
        goto err;
    }
    if (mem_gib > 0) {
        if (json_object_set_new (o,
                                 "memory",
                                 json_integer ((json_int_t)mem_gib)) < 0) {
            errprintf (errp, "failed to add memory to NUMA leaf JSON object");
            goto err;
        }
        ctx->found_mem = true;
    }
    goto out;
err:
    ERRNO_SAFE_WRAP (json_decref, o);
    o = NULL;
out:
    ERRNO_SAFE_WRAP (free, cores);
    ERRNO_SAFE_WRAP (free, gpu_ids);
    return o;
}

/* Forward declaration for recursive calls */
static json_t *build_topo_obj (struct topo_build_ctx *ctx,
                               hwloc_obj_t obj,
                               flux_error_t *errp);

/* Attach residual GPUs (not claimed by children) to a topo object.
 * Returns 0 on success, -1 on failure with errno and errp set.
 */
static int attach_residual_gpus (json_t *topo_obj,
                                 hwloc_obj_t obj,
                                 struct topo_build_ctx *ctx,
                                 flux_error_t *errp)
{
    char *gpu_ids = residual_gpus_for_obj (obj,
                                           ctx->gpus,
                                           ctx->ngpus,
                                           ctx->emitted_gpus);
    if (gpu_ids) {
        if (json_object_set_new (topo_obj, "gpus", json_string (gpu_ids)) < 0) {
            ERRNO_SAFE_WRAP (free, gpu_ids);
            errprintf (errp, "failed to add residual gpus to topo object");
            return -1;
        }
        ERRNO_SAFE_WRAP (free, gpu_ids);
    }
    return 0;
}

/* Attach machine total memory to node-level topo object as fallback.
 * Used when no lower-level objects reported memory (e.g., NUMA node spans
 * multiple packages). Only attaches if found_mem is false and total > 0.
 * Returns 0 on success, -1 on failure with errno and errp set.
 */
static int attach_fallback_memory (json_t *node_obj,
                                   uint64_t total_machine_mem,
                                   bool found_mem,
                                   flux_error_t *errp)
{
    if (found_mem || total_machine_mem == 0)
        return 0;

    uint64_t mem_gib = total_machine_mem / BYTES_PER_GIB;
    if (mem_gib > 0) {
        if (json_object_set_new (node_obj,
                                 "memory",
                                 json_integer ((json_int_t)mem_gib)) < 0) {
            errprintf (errp, "failed to add memory to topo object");
            return -1;
        }
    }
    return 0;
}

/* Attach any unplaced GPUs to the node-level topo object.
 * GPUs that were never assigned at any lower scope level are collected
 * and placed at node scope as a fallback.
 * Returns 0 on success, -1 on failure with errno and errp set.
 */
static int attach_unplaced_gpus (json_t *node_obj,
                                 struct topo_build_ctx *ctx,
                                 flux_error_t *errp)
{
    struct idset *node_ids;
    char *s = NULL;
    int rc = -1;

    if (ctx->ngpus <= 0)
        return 0;

    if (!(node_ids = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp, "failed to create node_ids idset");
        return -1;
    }

    for (int g = 0; g < ctx->ngpus; g++) {
        if (!idset_test (ctx->emitted_gpus, g)) {
            if (idset_set (node_ids, g) < 0) {
                errprintf (errp, "failed to set node_ids idset");
                goto out;
            }
        }
    }

    if (idset_count (node_ids) > 0) {
        if (!(s = idset_encode (node_ids, IDSET_FLAG_RANGE))) {
            errprintf (errp, "failed to encode node ids");
            goto out;
        }
        if (json_object_set_new (node_obj, "gpus", json_string (s)) < 0) {
            errprintf (errp, "failed to add gpus to topo object");
            goto out;
        }
    }

    rc = 0;
out:
    ERRNO_SAFE_WRAP (free, s);
    idset_destroy (node_ids);
    return rc;
}

/* Unwrap transparent Group wrappers to find the real hardware boundary name.
 * hwloc 2.x may insert Group around each NUMANode→Package from 1.x XML.
 * Returns the topology name for the first actual hardware container found.
 */
static const char *unwrap_group_name (struct topo_build_ctx *ctx,
                                      hwloc_obj_t *containers,
                                      int ncontainers)
{
    const char *name = object_topo_name (containers[0]);

    if (containers[0]->type != HWLOC_OBJ_GROUP)
        return name;

    hwloc_obj_t probe = containers[0];
    while (probe->type == HWLOC_OBJ_GROUP) {
        hwloc_obj_t inner;
        if (find_cpu_containers (probe, probe->cpuset, &inner, 1) != 1) {
            /* No CPU containers; check if Group wraps NUMA nodes */
            int nnuma = collect_numa_for_cpuset (ctx->topo,
                                                 probe->cpuset,
                                                 NULL,
                                                 0);
            if (nnuma > 0)
                return "numa";
            break;
        }
        probe = inner;
    }
    if (probe != containers[0])
        name = object_topo_name (probe);
    return name;
}

/* Build a topo object from a list of CPU containers.
 * Single container is folded (recursive); multiple are grouped under type name.
 * Sets errno on failure.
 */
static json_t *build_containers_topo (struct topo_build_ctx *ctx,
                                      hwloc_obj_t obj,
                                      hwloc_obj_t *containers,
                                      int ncontainers,
                                      flux_error_t *errp)
{
    json_t *result = NULL;

    if (ncontainers == 1) {
        /* Single container: fold without adding a wrapper level. */
        result = build_topo_obj (ctx, containers[0], errp);
        /* errno already set by recursive call */
        return result;
    }

    /* Multiple containers: build array under type name */
    const char *name = unwrap_group_name (ctx, containers, ncontainers);
    json_t *arr = json_array ();
    if (!arr) {
        errprintf (errp, "failed to create container array");
        return NULL;
    }

    for (int i = 0; i < ncontainers; i++) {
        json_t *child = build_topo_obj (ctx, containers[i], errp);
        if (!child) {
            /* errno already set by recursive call */
            json_decref (arr);
            return NULL;
        }
        if (json_array_append_new (arr, child) < 0) {
            json_decref (arr);
            errprintf (errp, "failed to append container to array");
            return NULL;
        }
    }

    if (!(result = json_object ())
        || json_object_set_new (result, name, arr) < 0) {
        ERRNO_SAFE_WRAP (json_decref, result);
        /* arr reference stolen by json_object_set_new even on failure */
        errprintf (errp, "failed to create container result object");
        return NULL;
    }

    /* Attach residual GPUs not claimed by any child */
    if (attach_residual_gpus (result, obj, ctx, errp) < 0) {
        ERRNO_SAFE_WRAP (json_decref, result);
        return NULL;
    }

    return result;
}

/* Build a topo object from NUMA nodes within a scope.
 * Single NUMA is folded into leaf; multiple are grouped under "numa".
 * Sets errno on failure.
 */
static json_t *build_numa_topo (struct topo_build_ctx *ctx,
                                hwloc_obj_t obj,
                                hwloc_obj_t *numas,
                                int nnuma,
                                flux_error_t *errp)
{
    json_t *result = NULL;

    if (nnuma == 1) {
        /* Single NUMA: fold into leaf. */
        result = numa_leaf_json (ctx, numas[0], errp);
        /* errno already set by numa_leaf_json */
        return result;
    }

    /* Multiple NUMA: build array */
    json_t *numa_arr = json_array ();
    if (!numa_arr) {
        errprintf (errp, "failed to create NUMA array");
        return NULL;
    }

    for (int i = 0; i < nnuma; i++) {
        json_t *leaf = numa_leaf_json (ctx, numas[i], errp);
        if (!leaf) {
            /* errno already set by numa_leaf_json */
            json_decref (numa_arr);
            return NULL;
        }
        if (json_array_append_new (numa_arr, leaf) < 0) {
            json_decref (numa_arr);
            errprintf (errp, "failed to append NUMA leaf to array");
            return NULL;
        }
    }

    if (!(result = json_object ())
        || json_object_set_new (result, "numa", numa_arr) < 0) {
        ERRNO_SAFE_WRAP (json_decref, result);
        /* numa_arr reference stolen by json_object_set_new even on failure */
        errprintf (errp, "failed to create NUMA result object");
        return NULL;
    }

    /* Attach residual GPUs not placed by any NUMA child */
    if (attach_residual_gpus (result, obj, ctx, errp) < 0) {
        ERRNO_SAFE_WRAP (json_decref, result);
        return NULL;
    }

    return result;
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
static json_t *build_topo_obj (struct topo_build_ctx *ctx,
                               hwloc_obj_t obj,
                               flux_error_t *errp)
{
    json_t *result = NULL;

    /* Try CPU containers (Package, Die, Group, ...) */
    int ncontainers = find_cpu_containers (obj, obj->cpuset, NULL, 0);
    if (ncontainers > 0) {
        hwloc_obj_t *containers = calloc (ncontainers, sizeof (*containers));
        if (!containers) {
            errprintf (errp, "failed to allocate containers array");
            return NULL;
        }
        find_cpu_containers (obj, obj->cpuset, containers, ncontainers);

        result = build_containers_topo (ctx, obj, containers, ncontainers, errp);
        ERRNO_SAFE_WRAP (free, containers);
        return result;
    }

    /* No CPU containers: try NUMA nodes */
    int nnuma = collect_numa_for_cpuset (ctx->topo, obj->cpuset, NULL, 0);
    if (nnuma == 0) {
        /* No NUMA nodes: emit leaf directly */
        return cpuset_leaf_json (ctx, obj, errp);
    }

    hwloc_obj_t *numas = calloc (nnuma, sizeof (*numas));
    if (!numas) {
        errprintf (errp, "failed to allocate NUMA array");
        return NULL;
    }
    collect_numa_for_cpuset (ctx->topo, obj->cpuset, numas, nnuma);

    result = build_numa_topo (ctx, obj, numas, nnuma, errp);
    ERRNO_SAFE_WRAP (free, numas);
    return result;
}

/* Build a TreePool topo object from hwloc topology.
 * Returns the topo object (not the complete scheduling key).
 * Sets errno and errp on failure.
 */
json_t *rhwloc_treepool_topo (hwloc_topology_t topo, flux_error_t *errp)
{
    hwloc_obj_t machine;
    json_t *node_obj = NULL;
    json_t *result = NULL;
    uint64_t total_machine_mem = 0;
    struct topo_build_ctx ctx = {
        .topo = topo,
        .gpus = NULL,
        .ngpus = 0,
        .emitted_gpus = NULL,
        .found_mem = false,
    };

    if (!topo) {
        errprintf (errp, "topo is NULL");
        errno = EINVAL;
        return NULL;
    }

    machine = hwloc_get_root_obj (topo);
    if (!machine || !machine->cpuset) {
        errprintf (errp, "no machine root object in topology");
        errno = EINVAL;
        return NULL;
    }

    /* rhwloc_gpu_objects() returns NULL both when there are no GPUs (ENODEV
     * or zero unique GPUs) and on allocation failure (ENOMEM).  Distinguish
     * the two so a genuine failure is not silently treated as "no GPUs",
     * which would emit a node with its GPUs missing.
     */
    errno = 0;
    ctx.gpus = rhwloc_gpu_objects (topo, &ctx.ngpus);
    if (!ctx.gpus && errno == ENOMEM) {
        errprintf (errp, "could not collect GPU objects: %s", strerror (errno));
        goto out;
    }

    int nnuma_total = hwloc_get_nbobjs_by_type (topo, HWLOC_OBJ_NUMANODE);
    for (int i = 0; i < nnuma_total; i++) {
        hwloc_obj_t n = hwloc_get_obj_by_type (topo, HWLOC_OBJ_NUMANODE, i);
        /* attr is guaranteed for a real NUMANODE, but guard anyway since the
         * topology may originate from untrusted XML. */
        if (n && n->attr)
            total_machine_mem += n->attr->numanode.local_memory;
    }

    if (!(ctx.emitted_gpus = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        errprintf (errp, "could not create gpus idset: %s", strerror (errno));
        goto out;
    }

    if (!(node_obj = build_topo_obj (&ctx, machine, errp)))
        goto out;

    /* Fallback: attach machine total memory and unplaced GPUs at node scope. */
    if (attach_fallback_memory (node_obj,
                                total_machine_mem,
                                ctx.found_mem,
                                errp) < 0)
        goto out;
    if (attach_unplaced_gpus (node_obj, &ctx, errp) < 0)
        goto out;

    result = node_obj;
    node_obj = NULL;
out:
    ERRNO_SAFE_WRAP (idset_destroy, ctx.emitted_gpus);
    ERRNO_SAFE_WRAP (json_decref, node_obj);
    ERRNO_SAFE_WRAP (free, ctx.gpus);
    return result;
}

char *rhwloc_treepool_topo_to_json (const char *xml, flux_error_t *errp)
{
    hwloc_topology_t topo;
    json_t *topo_json = NULL;
    char *result = NULL;

    if (!xml) {
        errno = EINVAL;
        errprintf (errp, "xml argument is NULL");
        return NULL;
    }

    if (!(topo = rhwloc_xml_topology_load (xml, RHWLOC_NO_RESTRICT))) {
        errprintf (errp, "failed to load hwloc topology from XML");
        return NULL;
    }

    if (!(topo_json = rhwloc_treepool_topo (topo, errp)))
        goto done;

    if (!(result = json_dumps (topo_json, JSON_COMPACT))) {
        errprintf (errp, "json_dumps failed");
        errno = ENOMEM;
        goto done;
    }

done:
    ERRNO_SAFE_WRAP (json_decref, topo_json);
    ERRNO_SAFE_WRAP (hwloc_topology_destroy, topo);
    return result;
}

/* vi: ts=4 sw=4 expandtab
 */
