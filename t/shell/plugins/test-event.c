/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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
#include <stdio.h>
#include <stdarg.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

static int get_shell_rank (flux_shell_t *shell)
{
    int rank = -1;
    char *json_str = NULL;
    json_t *o = NULL;

    if (flux_shell_get_info (shell, &json_str) < 0
        || !(o = json_loads (json_str, 0, NULL))
        || json_unpack (o, "{s:i}", "rank", &rank) < 0)
        shell_log_errno ("failed to get shell rank");
    json_decref (o);
    free (json_str);
    return rank;
}

static int check_event_context (flux_plugin_t *p,
                                const char *topic,
                                flux_plugin_arg_t *args,
                                void *data)
{
    flux_shell_t *shell = data;
    if (get_shell_rank (shell) != 0)
        return 0;
    return flux_shell_add_event_context (shell, "shell.init", 0,
                                       " {s:s}",
                                         "event-test", "foo");
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_plugin_set_name (p, "event-test");
    return flux_plugin_add_handler (p,
                                    "shell.init",
                                    check_event_context,
                                    flux_plugin_get_shell (p));
}

/*
 * vi: ts=4 sw=4 expandtab
 */
