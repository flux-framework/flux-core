/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job shell builtin plugin loader */

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "src/common/libutil/log.h"

#include "internal.h"
#include "builtins.h"
#include "plugstack.h"

static struct shell_builtin builtin_list_end = { 0 };

/*
 *  Unfortunate manually maintained list of builtins below.
 *  Builtin module should export a struct shell_builtin under a
 *  well known name, with that name listed as 'extern' below.
 *  Then, the name should be added to the 'builtins' list
 *  to get the builtin automatically loaded at shell startup.
 */
extern struct shell_builtin builtin_pmi;
extern struct shell_builtin builtin_input;
extern struct shell_builtin builtin_output;
extern struct shell_builtin builtin_kill;
extern struct shell_builtin builtin_signals;
extern struct shell_builtin builtin_affinity;
extern struct shell_builtin builtin_gpubind;

static struct shell_builtin * builtins [] = {
    &builtin_pmi,
    &builtin_input,
    &builtin_output,
    &builtin_kill,
    &builtin_signals,
    &builtin_affinity,
    &builtin_gpubind,
    &builtin_list_end,
};

static int shell_load_builtin (flux_shell_t *shell,
                               struct shell_builtin *sb)
{
    flux_plugin_t *p = flux_plugin_create ();
    if (!p)
        return -1;

    if (flux_plugin_aux_set (p, "flux::shell", shell, NULL) < 0
        || flux_plugin_set_name (p, sb->name) < 0
        || flux_plugin_add_handler (p, "shell.validate", sb->validate, NULL) < 0
        || flux_plugin_add_handler (p, "shell.init", sb->init, NULL) < 0
        || flux_plugin_add_handler (p, "shell.exit", sb->exit, NULL) < 0
        || flux_plugin_add_handler (p, "task.init",  sb->task_init, NULL) < 0
        || flux_plugin_add_handler (p, "task.fork",  sb->task_fork, NULL) < 0
        || flux_plugin_add_handler (p, "task.exec",  sb->task_exec, NULL) < 0
        || flux_plugin_add_handler (p, "task.exit",  sb->task_exit, NULL) < 0)
        return -1;

    if (shell->verbose)
        log_msg ("loading builtin plugin \"%s\"", sb->name);
    if (plugstack_push (shell->plugstack, p) < 0)
        return -1;
    return (0);
}

/*  Load all statically compiled shell "builtin" plugins
 */
int shell_load_builtins (flux_shell_t *shell)
{
    struct shell_builtin **sb = &builtins[0];
    while ((*sb)->name) {
        if (shell_load_builtin (shell, *sb) < 0)
            return -1;
        sb++;
    }
    return 0;
}

/* vi: ts=4 sw=4 expandtab
 */

