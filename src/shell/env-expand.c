/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define FLUX_SHELL_PLUGIN_NAME "env-expand"

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <jansson.h>

#include <flux/shell.h>

#include "builtins.h"

static int env_expand (flux_plugin_t *p,
                       const char *topic,
                       flux_plugin_arg_t *args,
                       void *data)
{
    json_t *to_expand = NULL;
    json_t *value;
    const char *key;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (!(shell = flux_plugin_get_shell (p)))
        return shell_log_errno ("unable to get shell handle");
    if (flux_shell_getopt_unpack (shell, "env-expand", "o", &to_expand) != 1)
        return 0;
    json_object_foreach (to_expand, key, value) {
        const char *s = json_string_value (value);
        char *result;
        if (s == NULL) {
            shell_log_error ("invalid value for env var %s", key);
            continue;
        }
        if (!(result = flux_shell_mustache_render (shell, s))) {
            shell_log_errno ("failed to expand env var %s=%s", key, s);
            continue;
        }
        if (flux_shell_setenvf (shell, 1, key, "%s", result) < 0)
            shell_log_errno ("failed to set %s=%s", key, result);
        free (result);
    }
    return 0;
}

struct shell_builtin builtin_env_expand = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = env_expand,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
