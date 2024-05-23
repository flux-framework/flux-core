/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SHELL_BUILTINS_H
#define _SHELL_BUILTINS_H

#include <flux/core.h>
#include <flux/shell.h>

struct shell_builtin {
    const char *name;
    int (*plugin_init) (flux_plugin_t *p);
    flux_plugin_f validate;
    flux_plugin_f connect;
    flux_plugin_f reconnect;
    flux_plugin_f init;
    flux_plugin_f post_init;
    flux_plugin_f task_init;
    flux_plugin_f task_exec;
    flux_plugin_f task_fork;
    flux_plugin_f start;
    flux_plugin_f task_exit;
    flux_plugin_f exit;
};

/*  Load all statically compiled shell "builtin" plugins
 */
int shell_load_builtins (flux_shell_t *shell);

#endif /* !_SHELL_BUILTINS_H */

/* vi: ts=4 sw=4 expandtab
 */

