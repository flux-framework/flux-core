/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* slot-info - print RFC 20 slot info from the shell plugin API
 *
 * At shell.init, print the slots key of rank info for each shell rank.
 * At task.init, print the slot key of task info for each task.
 * If R has no slots, print "none".
 */
#define FLUX_SHELL_PLUGIN_NAME "slot-info"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

static void print_json (const char *prefix, json_t *o)
{
    char *s = o ? json_dumps (o, JSON_COMPACT | JSON_SORT_KEYS) : NULL;
    shell_log ("%s: %s", prefix, s ? s : "none");
    free (s);
}

static int slot_info_init (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    char *s;
    json_t *o;

    if (flux_shell_get_rank_info (shell, -1, &s) < 0)
        return shell_log_errno ("flux_shell_get_rank_info");
    if (!(o = json_loads (s, 0, NULL))) {
        free (s);
        shell_log_error ("failed to parse rank info");
        return -1;
    }
    free (s);
    print_json ("slots", json_object_get (o, "slots"));
    json_decref (o);
    return 0;
}

static int slot_info_task_init (flux_plugin_t *p,
                                const char *topic,
                                flux_plugin_arg_t *args,
                                void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task = flux_shell_current_task (shell);
    json_t *slot = NULL;
    char prefix[64];
    int rank;

    if (flux_shell_task_info_unpack (task,
                                     "{s:i s?o}",
                                     "rank", &rank,
                                     "slot", &slot) < 0)
        return shell_log_errno ("flux_shell_task_info_unpack");
    snprintf (prefix, sizeof (prefix), "task %d slot", rank);
    print_json (prefix, slot);
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    if (flux_plugin_add_handler (p, "shell.init", slot_info_init, NULL) < 0
        || flux_plugin_add_handler (p,
                                    "task.init",
                                    slot_info_task_init,
                                    NULL) < 0)
        return -1;
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
