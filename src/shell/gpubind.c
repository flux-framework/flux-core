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
    hwloc_cpuset_t *gpusets;
};


static void gpu_affinity_destroy (struct gpu_affinity *ctx)
{
    if (ctx) {
        idset_destroy (ctx->gpus);
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
    if (!(ctx->gpus = idset_decode (gpu_list ? gpu_list : ""))) {
        shell_log_errno ("idset_encode (%s)", gpu_list);
        goto error;
    }
    ctx->ngpus = idset_count (ctx->gpus);
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
    if (ctx->ngpus <= 0)
        return 0;

    if (flux_plugin_add_handler (p,
                                 "task.init",
                                 gpubind_task_init,
                                 ctx) < 0)
        return shell_log_errno ("gpubind: flux_plugin_add_handler");

    flux_shell_setenvf (shell, 0, "CUDA_DEVICE_ORDER", "PCI_BUS_ID");

    if (streq (opt, "per-task")) {
        if (!(ctx->gpusets = distribute_gpus (ctx)))
            return shell_log_errno ("failed to distribute %d gpus",
                                    ctx->ngpus);
    }
    else {
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
