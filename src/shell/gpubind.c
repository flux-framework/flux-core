/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* GPU binding plugin
 *
 * Builtin GPU binding for flux-shell. Spread CUDA_VISIBLE_DEVICES
 *  across tasks depending on number in slot.
 */
#define FLUX_SHELL_PLUGIN_NAME "gpu-affinity"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "ccan/str/str.h"

#include "builtins.h"
#include "affinity.h"

struct gpu_affinity {
    int ntasks;
    int ngpus;
    struct idset *gpus;
    struct idset *rank_gpus;    /* GPU ids from R, before any remap */
    bool constrained;
    bool slots;                 /* gpusets are from RFC 20 slots in R */
    hwloc_cpuset_t *gpusets;
};


static void gpu_affinity_destroy (struct gpu_affinity *ctx)
{
    if (ctx) {
        idset_destroy (ctx->gpus);
        idset_destroy (ctx->rank_gpus);
        cpuset_array_destroy (ctx->gpusets, ctx->ntasks);
        free (ctx);
    }
}

static struct gpu_affinity *gpu_affinity_create (flux_shell_t *shell)
{
    const char *gpu_list = NULL;
    struct gpu_affinity *ctx = calloc (1, sizeof (*ctx));

    if (!ctx)
        return NULL;
    if (flux_shell_rank_info_unpack (shell,
                                     -1,
                                     "{s:i s:{s?s}}",
                                     "ntasks", &ctx->ntasks,
                                     "resources",
                                       "gpus", &gpu_list) < 0) {
        shell_log_errno ("flux_shell_rank_info_unpack");
        goto error;
    }
    if (!(ctx->gpus = idset_decode (gpu_list ? gpu_list : ""))
        || !(ctx->rank_gpus = idset_copy (ctx->gpus))) {
        shell_log_errno ("idset_decode (%s)", gpu_list);
        goto error;
    }
    ctx->ngpus = idset_count (ctx->gpus);

    /*  Under device containment the cgroup denies access to the device
     *  nodes of unallocated GPUs, and the GPU runtimes enumerate only
     *  the devices they can open, indexing them from zero. Verified with
     *  ROCm and CUDA on AMD MI300X and NVIDIA H100 systems.
     *  (see flux-framework/flux-core#7790).
     */
    if (ctx->ngpus > 0) {
        int constrained = 0;

        if (flux_shell_info_unpack (shell,
                                   "{s:b}",
                                   "constrained_resources",
                                   &constrained) < 0) {
            shell_log_errno ("flux_shell_info_unpack");
            goto error;
        }
        if (constrained) {
            struct idset *rel;

            ctx->constrained = true;
            if (!(rel = idset_create (0, IDSET_FLAG_AUTOGROW))) {
                shell_log_errno ("idset_create");
                goto error;
            }
            if (idset_range_set (rel, 0, ctx->ngpus - 1) < 0) {
                shell_log_errno ("idset_range_set");
                idset_destroy (rel);
                goto error;
            }
            shell_debug ("device containment active: remap gpus: %s to %d-%d",
                         gpu_list,
                         0,
                         ctx->ngpus - 1);
            idset_destroy (ctx->gpus);
            ctx->gpus = rel;
        }
    }
    return ctx;
error:
    gpu_affinity_destroy (ctx);
    return NULL;
}

static int plugin_task_setenv (flux_plugin_t *p,
                               const char *var,
                               const char *val)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    flux_cmd_t *cmd = flux_shell_task_cmd (task);
    if (cmd)
        return flux_cmd_setenvf (cmd, 1, var, "%s", val);
    return 0;
}

static int plugin_task_id (flux_plugin_t *p)
{
    int taskid = -1;
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    if (flux_shell_task_info_unpack (task,
                                     "{s:i}",
                                     "localid", &taskid) < 0)
        return shell_log_errno ("failed to unpack task local id");
    return taskid;
}

static struct idset *cpuset_to_idset (hwloc_cpuset_t set)
{
    int i;
    struct idset *idset;
    if (!(idset = idset_create (0, IDSET_FLAG_AUTOGROW))) {
        shell_log_errno ("failed to create idset");
        return NULL;
    }
    i = -1;
    while ((i = hwloc_bitmap_next (set, i)) != -1) {
        if (idset_set (idset, i) < 0) {
            shell_log_errno ("failed to set %d in idset", i);
            idset_destroy (idset);
            return NULL;
        }
    }
    return idset;
}

static int gpubind_task_init (flux_plugin_t *p,
                              const char *topic,
                              flux_plugin_arg_t *args,
                              void *data)
{
    char *s;
    struct idset *ids;
    int taskid;
    struct gpu_affinity *ctx = data;

    if (!ctx->gpusets)
        return 0;

    if ((taskid = plugin_task_id (p)) < 0)
        return -1;

    /* A task in a slot with no GPUs keeps CUDA_VISIBLE_DEVICES=-1
     */
    if (ctx->slots && hwloc_bitmap_iszero (ctx->gpusets[taskid]))
        return 0;

    /* Need to convert hwloc_cpuset_t to idset since there's no function
     * to convert a hwloc_cpuset_t to a strict comma-separated list of ids:
     */
    if (!(ids = cpuset_to_idset (ctx->gpusets[taskid]))
        || !(s = idset_encode (ids, 0))) {
        shell_log_error ("failed to get idset from gpu set for task %d",
                         taskid);
        idset_destroy (ids);
        return -1;
    }
    plugin_task_setenv (p, "CUDA_VISIBLE_DEVICES", s);
    free (s);
    idset_destroy (ids);
    return 0;
}

/*  Return the index of `id` in `ids`, or -1 if `id` is not in `ids`.
 */
static int idset_index (const struct idset *ids, unsigned int id)
{
    int index = 0;
    unsigned int i = idset_first (ids);
    while (i != IDSET_INVALID_ID) {
        if (i == id)
            return index;
        index++;
        i = idset_next (ids, i);
    }
    return -1;
}

/*  Set the GPUs in `gpuids`, an RFC 22 idset string of GPU ids from R,
 *   in `gpuset`. Under device containment, set the position of each GPU
 *   within this rank's GPUs instead, as for the whole rank.
 */
static int gpuset_set_slot_gpus (struct gpu_affinity *ctx,
                                 hwloc_cpuset_t gpuset,
                                 const char *gpuids)
{
    struct idset *ids;
    unsigned int id;
    int rc = -1;

    if (!(ids = idset_decode (gpuids))) {
        shell_log_error ("invalid slot gpus %s", gpuids);
        return -1;
    }
    for (id = idset_first (ids);
         id != IDSET_INVALID_ID;
         id = idset_next (ids, id)) {
        int gpu = id;
        if (ctx->constrained
            && (gpu = idset_index (ctx->rank_gpus, id)) < 0) {
            shell_log_error ("slot gpu %u not allocated to this rank", id);
            goto out;
        }
        hwloc_bitmap_set (gpuset, gpu);
    }
    rc = 0;
out:
    idset_destroy (ids);
    return rc;
}

/*  Return a per-task gpuset array using the GPUs of each task's RFC 20
 *   slot. Tasks that share a slot share its GPUs.
 */
static hwloc_cpuset_t *slot_gpusets (flux_shell_t *shell,
                                     struct gpu_affinity *ctx)
{
    hwloc_cpuset_t *gpusets;
    flux_shell_task_t *task;

    if (!(gpusets = cpuset_array_create (ctx->ntasks)))
        return NULL;
    for (task = flux_shell_task_first (shell);
         task != NULL;
         task = flux_shell_task_next (shell)) {
        int localid;
        const char *gpuids;

        if (flux_shell_task_info_unpack (task,
                                         "{s:i s:{s:s}}",
                                         "localid", &localid,
                                         "slot",
                                           "gpus", &gpuids) < 0
            || localid < 0
            || localid >= ctx->ntasks) {
            shell_log_error ("failed to get task slot");
            goto error;
        }
        if (gpuset_set_slot_gpus (ctx, gpusets[localid], gpuids) < 0)
            goto error;
    }
    return gpusets;
error:
    cpuset_array_destroy (gpusets, ctx->ntasks);
    return NULL;
}

static hwloc_cpuset_t *distribute_gpus (struct gpu_affinity *ctx)
{
    int ngpus_per_task = ctx->ngpus / ctx->ntasks;
    hwloc_cpuset_t *gpusets = cpuset_array_create (ctx->ntasks);
    for (int i = 0; i < ctx->ntasks; i++) {
        for (int j = 0; j < ngpus_per_task; j++) {
            unsigned id = idset_first (ctx->gpus);
            if (id == IDSET_INVALID_ID
                || idset_clear (ctx->gpus, id) < 0) {
                shell_log_errno ("Failed to get GPU id for task %d", id);
                goto error;
            }
            hwloc_bitmap_set (gpusets[i], id);
        }
    }
    return gpusets;
error:
    cpuset_array_destroy (gpusets, ctx->ntasks);
    return NULL;
}

static int gpubind_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    int rc;
    char *opt;
    struct gpu_affinity *ctx;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!shell)
        return -1;

    if ((rc = flux_shell_getopt_unpack (shell,
                                       "gpu-affinity",
                                       "s",
                                        &opt)) <= 0) {
        if (rc < 0)
            shell_warn ("Failed to get gpu-affinity shell option, ignoring");
        /* gpu-affinity defaults to "on" */
        opt = "on";
    }
    if (streq (opt, "off")) {
        shell_debug ("disabling affinity due to gpu-affinity=off");
        return 0;
    }

    /*  Set default CUDA_VISIBLE_DEVICES to an invalid id, -1, so that
     *  jobs which are not assigned any GPUs do not use GPUs which
     *  happen to be available on the current node.
     */
    flux_shell_setenvf (shell, 1, "CUDA_VISIBLE_DEVICES", "%d", -1);

    if (!(ctx = gpu_affinity_create (shell)))
        return -1;
    if (flux_plugin_aux_set (p,
                             NULL,
                             ctx,
                             (flux_free_f) gpu_affinity_destroy) < 0) {
        shell_log_errno ("flux_plugin_aux_set");
        gpu_affinity_destroy (ctx);
        return -1;
    }

    if (flux_plugin_add_handler (p,
                                 "task.init",
                                 gpubind_task_init,
                                 ctx) < 0)
        return shell_log_errno ("gpubind: flux_plugin_add_handler");

    flux_shell_setenvf (shell, 0, "CUDA_DEVICE_ORDER", "PCI_BUS_ID");

    /*  If gpu-affinity=per-task, then assign each task the GPUs of its
     *   slot if the scheduler assigned slots in R. Otherwise, divide
     *   GPUs among tasks.
     */
    if (streq (opt, "per-task")) {
        if ((ctx->slots = shell_has_slots (shell))) {
            if (!(ctx->gpusets = slot_gpusets (shell, ctx)))
                return shell_log_errno ("failed to assign slot gpus");
        }
        else if (!(ctx->gpusets = distribute_gpus (ctx)))
            return shell_log_errno ("failed to distribute %d gpus",
                                    ctx->ngpus);
    }
    else if (strstarts (opt, "map:")) {
        if (!(ctx->gpusets = parse_cpuset_list (opt+4, ctx->ntasks)))
            return shell_log_errno ("failed to parse gpu map %s", opt+4);
    }
    else if (ctx->ngpus > 0) {
        char *ids = idset_encode (ctx->gpus, 0);
        flux_shell_setenvf (shell, 1, "CUDA_VISIBLE_DEVICES", "%s", ids);
        free (ids);
    }
    return 0;
}

struct shell_builtin builtin_gpubind = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = gpubind_init
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
