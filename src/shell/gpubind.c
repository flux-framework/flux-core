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
 * Builtin GPU binding for flux-shell. Spread the GPU visibility
 *  environment variable (e.g. CUDA_VISIBLE_DEVICES) across tasks
 *  depending on number in slot.
 */
#define FLUX_SHELL_PLUGIN_NAME "gpu-affinity"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <flux/core.h>
#include <flux/shell.h>
#include <flux/idset.h>

#include "ccan/array_size/array_size.h"
#include "ccan/str/str.h"
#include "src/common/librlist/rhwloc.h"

#include "builtins.h"
#include "affinity.h"

struct gpu_affinity {
    int ntasks;
    int ngpus;
    struct idset *gpus;
    hwloc_cpuset_t *gpusets;
    const char *envvar;
};

/*  GPU visibility environment variable to set for each value of the
 *  gpu-env shell option. The variable for the other vendor is unset so
 *  that a value inherited from the submission environment cannot select
 *  GPUs. For example, the ROCm HIP runtime applies CUDA_VISIBLE_DEVICES
 *  after ROCR_VISIBLE_DEVICES has already renumbered the visible devices.
 */
static const struct gpu_env {
    const char *name;
    unsigned int vendor;
    const char *envvar;
    const char *unset;
} gpu_envs[] = {
    {
        .name = "nvidia",
        .vendor = RHWLOC_PCI_VENDOR_NVIDIA,
        .envvar = "CUDA_VISIBLE_DEVICES",
        .unset = "ROCR_VISIBLE_DEVICES",
    },
    {
        .name = "amd",
        .vendor = RHWLOC_PCI_VENDOR_AMD,
        .envvar = "ROCR_VISIBLE_DEVICES",
        .unset = "CUDA_VISIBLE_DEVICES",
    },
};

static const struct gpu_env *gpu_env_lookup (const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE (gpu_envs); i++) {
        if (streq (gpu_envs[i].name, name))
            return &gpu_envs[i];
    }
    return NULL;
}

/*  Return the gpu_env entry for the vendor of the GPUs assigned to this
 *  shell, or of all GPUs in the shell topology if none are assigned, so
 *  that the -1 default hides GPUs from jobs without GPUs. Use nvidia if
 *  there are no GPUs, the vendor is unknown, or GPUs come from different
 *  vendors.
 *
 *  If the job is assigned GPUs but their vendor is unknown, for example
 *  because the job shell topology does not include them, or the GPUs come
 *  from different vendors, the GPU runtime may not use the selected
 *  variable. Warn the user in that case.
 */
static const struct gpu_env *gpu_env_detect (flux_shell_t *shell)
{
    const char *gpu_list = NULL;
    struct idset *ids;
    const char *xml;
    hwloc_topology_t topo;
    unsigned int vendor = 0;
    bool have_gpus;

    /*  Use GPU ids from R, which index GPUs in the shell topology,
     *  not ctx->gpus, which may be renumbered under device containment.
     */
    if (flux_shell_rank_info_unpack (shell,
                                     -1,
                                     "{s:{s?s}}",
                                     "resources",
                                       "gpus", &gpu_list) < 0
        || !(ids = idset_decode (gpu_list ? gpu_list : ""))) {
        shell_log_errno ("failed to get GPU ids for gpu-env=auto");
        return NULL;
    }
    have_gpus = idset_count (ids) > 0;
    if (flux_shell_get_hwloc_xml (shell, &xml) == 0
        && (topo = rhwloc_xml_topology_load (xml, RHWLOC_NO_RESTRICT))) {
        vendor = rhwloc_gpu_vendor_id (topo, have_gpus ? ids : NULL);
        hwloc_topology_destroy (topo);
    }
    idset_destroy (ids);
    for (size_t i = 0; i < ARRAY_SIZE (gpu_envs); i++) {
        if (gpu_envs[i].vendor == vendor)
            return &gpu_envs[i];
    }
    if (have_gpus)
        shell_warn ("gpu-env=auto: GPU vendor unknown or mixed,"
                    " using nvidia (set gpu-env to override)");
    return gpu_env_lookup ("nvidia");
}

/*  Return the gpu_env entry selected by the gpu-env shell option.
 */
static const struct gpu_env *gpu_env_get (flux_shell_t *shell)
{
    const char *name = "auto";
    const struct gpu_env *env;

    if (flux_shell_getopt_unpack (shell, "gpu-env", "s", &name) < 0) {
        shell_log_error ("gpu-env shell option must be a string");
        return NULL;
    }
    if (streq (name, "auto")) {
        if (!(env = gpu_env_detect (shell)))
            return NULL;
        shell_debug ("gpu-env=auto: using %s", env->name);
        return env;
    }
    if (!(env = gpu_env_lookup (name))) {
        shell_log_error ("invalid gpu-env shell option: %s", name);
        return NULL;
    }
    return env;
}


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
    if (plugin_task_setenv (p, ctx->envvar, s) < 0) {
        shell_log_errno ("failed to set %s for task %d", ctx->envvar, taskid);
        free (s);
        idset_destroy (ids);
        return -1;
    }
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
    const struct gpu_env *env;
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

    if (!(env = gpu_env_get (shell)))
        return -1;

    /*  Set default GPU visibility to an invalid id, -1, so that
     *  jobs which are not assigned any GPUs do not use GPUs which
     *  happen to be available on the current node.
     */
    if (flux_shell_setenvf (shell, 1, env->envvar, "%d", -1) < 0)
        return shell_log_errno ("failed to set %s", env->envvar);
    if (flux_shell_unsetenv (shell, env->unset) < 0 && errno != ENOENT)
        return shell_log_errno ("failed to unset %s", env->unset);

    if (!(ctx = gpu_affinity_create (shell)))
        return -1;
    ctx->envvar = env->envvar;
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

    if (streq (opt, "per-task")) {
        if (!(ctx->gpusets = distribute_gpus (ctx)))
            return shell_log_errno ("failed to distribute %d gpus",
                                    ctx->ngpus);
    }
    else if (strstarts (opt, "map:")) {
        if (!(ctx->gpusets = parse_cpuset_list (opt+4, ctx->ntasks)))
            return shell_log_errno ("failed to parse gpu map %s", opt+4);
    }
    else if (ctx->ngpus > 0) {
        char *ids;
        if (!(ids = idset_encode (ctx->gpus, 0))
            || flux_shell_setenvf (shell, 1, env->envvar, "%s", ids) < 0) {
            free (ids);
            return shell_log_errno ("failed to set %s", env->envvar);
        }
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
