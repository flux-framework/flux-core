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
#define FLUX_SHELL_PLUGIN_NAME NULL

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include <flux/shell.h>

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
extern struct shell_builtin builtin_tmpdir;
extern struct shell_builtin builtin_files;
extern struct shell_builtin builtin_stage_in;
extern struct shell_builtin builtin_log_eventlog;
extern struct shell_builtin builtin_pmi;
extern struct shell_builtin builtin_input_service;
extern struct shell_builtin builtin_file_input;
extern struct shell_builtin builtin_kvs_input;
extern struct shell_builtin builtin_output;
extern struct shell_builtin builtin_kill;
extern struct shell_builtin builtin_signals;
extern struct shell_builtin builtin_affinity;
extern struct shell_builtin builtin_gpubind;
extern struct shell_builtin builtin_mpir;
extern struct shell_builtin builtin_ptrace;
extern struct shell_builtin builtin_pty;
extern struct shell_builtin builtin_batch;
extern struct shell_builtin builtin_doom;
extern struct shell_builtin builtin_exception;
extern struct shell_builtin builtin_rlimit;
extern struct shell_builtin builtin_cyclic;
extern struct shell_builtin builtin_hostfile;
extern struct shell_builtin builtin_signal;
extern struct shell_builtin builtin_oom;
extern struct shell_builtin builtin_hwloc;
extern struct shell_builtin builtin_rexec;
extern struct shell_builtin builtin_env_expand;
extern struct shell_builtin builtin_sysmon;

static struct shell_builtin * builtins [] = {
    &builtin_tmpdir,
    &builtin_files,
    &builtin_stage_in,
    &builtin_log_eventlog,
    &builtin_pmi,
    &builtin_input_service,
    &builtin_file_input,
    &builtin_kvs_input,
    &builtin_output,
    &builtin_kill,
    &builtin_signals,
    &builtin_affinity,
    &builtin_gpubind,
    &builtin_mpir,
    &builtin_ptrace,
    &builtin_pty,
    &builtin_batch,
    &builtin_doom,
    &builtin_exception,
    &builtin_rlimit,
    &builtin_cyclic,
    &builtin_hostfile,
    &builtin_signal,
#if HAVE_INOTIFY_INIT1
    &builtin_oom,
#endif
    &builtin_hwloc,
    &builtin_rexec,
    &builtin_env_expand,
    &builtin_sysmon,
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
        || flux_plugin_add_handler (p, "shell.connect",  sb->connect, NULL) < 0
        || flux_plugin_add_handler (p, "shell.reconnect",
                                    sb->reconnect, NULL) < 0
        || flux_plugin_add_handler (p, "shell.init", sb->init, NULL) < 0
        || flux_plugin_add_handler (p,
                                    "shell.post-init",
                                    sb->post_init,
                                    NULL) < 0
        || flux_plugin_add_handler (p, "shell.exit", sb->exit, NULL) < 0
        || flux_plugin_add_handler (p, "shell.finish", sb->finish, NULL) < 0
        || flux_plugin_add_handler (p, "shell.start",sb->start, NULL) < 0
        || flux_plugin_add_handler (p, "task.init",  sb->task_init, NULL) < 0
        || flux_plugin_add_handler (p, "task.fork",  sb->task_fork, NULL) < 0
        || flux_plugin_add_handler (p, "task.exec",  sb->task_exec, NULL) < 0
        || flux_plugin_add_handler (p, "task.exit",  sb->task_exit, NULL) < 0)
        return -1;

    shell_debug ("loading builtin plugin \"%s\"", sb->name);
    if (sb->plugin_init && (*sb->plugin_init) (p) < 0)
        return -1;
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

