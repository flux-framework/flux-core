/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* debugger support
 *
 *  If stop-tasks-in-exec option is set, then
 *
 *   1. In child, set PTRACE_TRACEME calling exec()
 *   2. In parent, wait for tasks to stop, send SIGSTOP
 *   3. In parent, detach ptrace(2) so SIGSTOP is delivered
 *   3. Add "sync=true" to the emitted `shell.start` event
 *       to indicate all tasks are now stopped in exec().
 */
#define FLUX_SHELL_PLUGIN_NAME "ptrace"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <signal.h>

#include <flux/core.h>
#include <flux/shell.h>

#include <jansson.h>

#include "builtins.h"

static int ptrace_traceme (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *data)
{
    /* If requested, stop process on exec with parent attached */
    ptrace (PTRACE_TRACEME, 0, NULL, 0);
    return 0;
}

int current_task_pid (flux_shell_t *shell)
{
    json_int_t pid = -1;
    if (flux_shell_task_info_unpack (flux_shell_current_task (shell),
                                     "{s:I}",
                                     "pid", &pid) < 0)
        return -1;
    return (int) pid;
}

static int ptrace_stop_task (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    int rc;
    int status = 0;
    flux_shell_t *shell = data;
    pid_t pid = current_task_pid (shell);

    if (pid < 0)
        return shell_log_errno ("failed to get current pid");

    shell_trace ("stop_task: waiting for pid %ld to stop", (long) pid);
    if ((rc = waitpid (pid, &status, WUNTRACED)) < 0)
        return shell_log_errno ("waitpid");
    shell_trace ("stop_task: waitpid returned status 0x%04x", status);
    if (WIFSTOPPED (status)) {
        /* Send SIGSTOP, then detach from process */
        if (kill (pid, SIGSTOP) < 0)
            return shell_log_errno ("debug_trace: kill");
        shell_trace ("stop_task: detaching from pid %ld", (long) pid);
        if (ptrace (PTRACE_DETACH, pid, NULL, 0) < 0)
            return shell_log_errno ("debug_trace: ptrace");
        return 0;
    }
    /*  O/w, did task exit? */
    if (WIFEXITED (status))
        shell_log_error ("task unexpectedly exited");
    else
        shell_log_error ("unexpected exit status 0x%04x", status);
    return -1;
}


static int ptrace_set_sync (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    flux_shell_t *shell = data;
    return flux_shell_add_event_context (shell, "shell.start", 0,
                                         "{s:b}",
                                         "sync", true);
}

static int ptrace_init (flux_plugin_t *p,
                        const char *topic,
                        flux_plugin_arg_t *args,
                        void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    int stop_in_exec = 0;
    char *s = NULL;

    if (!shell)
        return -1;

    if (flux_shell_getopt (shell, "stop-tasks-in-exec", &s)) {
        json_t *o = json_loads (s, JSON_DECODE_ANY, NULL);
        if (!o)
            shell_die (1, "Failed to decode stop-tasks-in-exec shell option");
        if (json_is_true (o)
           || (json_is_integer (o) && json_integer_value (o) > 0))
            stop_in_exec = 1;
        json_decref (o);
    }
    if (stop_in_exec
        && (flux_plugin_add_handler (p,
                                     "task.exec",
                                     ptrace_traceme,
                                     shell) < 0
        || flux_plugin_add_handler (p,
                                    "task.fork",
                                    ptrace_stop_task,
                                    shell) < 0
        || flux_plugin_add_handler (p,
                                    "shell.start",
                                    ptrace_set_sync,
                                    shell) < 0))
            shell_die_errno (1, "flux_plugin_add_handler");
    free (s);
    return 0;
}

struct shell_builtin builtin_ptrace = {
    .name =      FLUX_SHELL_PLUGIN_NAME,
    .init =      ptrace_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
