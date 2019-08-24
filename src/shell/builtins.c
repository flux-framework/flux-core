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

static struct shell_builtin * builtins [] = {
    &builtin_pmi,
    &builtin_list_end,
};

static int shell_load_builtin (flux_shell_t *shell,
                               struct shell_builtin *sb)
{
    struct splugin *p = splugin_create ();
    if (!p)
        return -1;

    if (splugin_set_name (p, sb->name) < 0
        || splugin_set_sym (p, "flux_shell_validate",  sb->validate) < 0
        || splugin_set_sym (p, "flux_shell_init",      sb->init) < 0
        || splugin_set_sym (p, "flux_shell_exit",      sb->exit) < 0
        || splugin_set_sym (p, "flux_shell_task_init", sb->task_init) < 0
        || splugin_set_sym (p, "flux_shell_task_fork", sb->task_fork) < 0
        || splugin_set_sym (p, "flux_shell_task_exec", sb->task_exec) < 0
        || splugin_set_sym (p, "flux_shell_task_exit", sb->task_exit) < 0)
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

