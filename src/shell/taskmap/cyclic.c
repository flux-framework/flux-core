/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell taskmap.cyclic plugin
 */
#define FLUX_SHELL_PLUGIN_NAME "taskmap.cyclic"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/taskmap.h>

#include "builtins.h"

char *taskmap_cyclic (const struct taskmap *orig, int stride)
{
    struct taskmap *map = NULL;
    char *result = NULL;
    int ntasks;
    int nnodes;

    if (!(map = taskmap_create ()))
        goto error;

    ntasks = taskmap_total_ntasks (orig);
    nnodes = taskmap_nnodes (orig);
    while (ntasks > 0) {
        for (int i = 0; i < nnodes; i++) {
            int ppn = stride;
            int avail = taskmap_ntasks (orig, i) - taskmap_ntasks (map, i);
            if (avail == 0)
                continue;
            if (ppn > avail)
                ppn = avail;
            if (taskmap_append (map, i, 1, ppn) < 0)
                goto error;
            ntasks -= ppn;
        }
    }
    result = taskmap_encode (map, TASKMAP_ENCODE_WRAPPED);
error:
    taskmap_destroy (map);
    return result;
}

static int map_cyclic (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    flux_shell_t *shell;
    char *cyclic;
    const char *value = NULL;
    int stride = 1;
    int rc = -1;

    if (!(shell = flux_plugin_get_shell (p)))
        return -1;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s?s}",
                                "value", &value) < 0) {
        shell_log_error ("unpack: %s", flux_plugin_arg_strerror (args));
        return -1;
    }
    if (value && *value != '\0') {
        char *endptr;
        errno = 0;
        stride = strtol (value, &endptr, 10);
        if (errno != 0 || *endptr != '\0' || stride <= 0) {
            shell_log_error ("invalid cyclic stride: %s", value);
            return -1;
        }
    }
    if (!(cyclic = taskmap_cyclic (flux_shell_get_taskmap (shell), stride))) {
        shell_log_error ("failed to map tasks with cyclic:%d", stride);
        goto out;
    }
    if (flux_plugin_arg_pack (args,
                              FLUX_PLUGIN_ARG_OUT,
                              "{s:s}",
                              "taskmap", cyclic) < 0)
        goto out;
    rc = 0;
out:
    free (cyclic);
    return rc;
}

static int plugin_init (flux_plugin_t *p)
{
    return flux_plugin_add_handler (p, "taskmap.cyclic", map_cyclic, NULL);
}

struct shell_builtin builtin_cyclic = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .plugin_init = plugin_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
