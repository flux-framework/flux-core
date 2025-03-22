/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "fork"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <flux/core.h>
#include <flux/shell.h>

static int init_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *data)
{
    if (fork () == 0) {
        /* Just pause in child to simulate a worker process spawned by
         * a shell plugin.
         */
        pause ();
        exit (0);
    }
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_add_handler (p, "shell.init", init_cb, NULL) < 0)
        return -1;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
