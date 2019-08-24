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

#include <flux/shell.h>

struct shell_builtin {
    const char *name;
    int (*validate)  (flux_shell_t *shell);
    int (*init)      (flux_shell_t *shell);
    int (*task_init) (flux_shell_t *shell);
    int (*task_exec) (flux_shell_t *shell);
    int (*task_fork) (flux_shell_t *shell);
    int (*task_exit) (flux_shell_t *shell);
    int (*exit)      (flux_shell_t *shell);
};

/*  Load all statically compiled shell "builtin" plugins
 */
int shell_load_builtins (flux_shell_t *shell);

#endif /* !_SHELL_BUILTINS_H */

/* vi: ts=4 sw=4 expandtab
 */

