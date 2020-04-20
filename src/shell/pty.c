/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
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

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>

#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libterminus/pty.h"
#include "src/common/libterminus/terminus.h"
#include "builtins.h"

static void shlog (void *arg,
                   const char *file,
                   int line,
                   const char *func,
                   const char *subsys,
                   int level,
                   const char *fmt,
                   va_list ap)
{
    char buf [4096];
    int buflen = sizeof (buf);
    int n = vsnprintf (buf, buflen, fmt, ap);
    if (n >= buflen) {
        buf[buflen-1] = '\0';
        buf[buflen-2] = '+';
    }
    flux_shell_log (level, file, line, "%s", buf);
}

static struct flux_terminus_server *
shell_terminus_server_start (flux_shell_t *shell, const char *shell_service)
{
    char service[128];
    struct flux_terminus_server *t;

    if (snprintf (service,
                  sizeof (service),
                  "%s.terminus",
                  shell_service) >= sizeof (service)) {
        shell_log_errno ("Failed to build terminus service name");
        return NULL;
    }

    /*  Create a terminus server in this shell. 1 per shell */
    t = flux_terminus_server_create (flux_shell_get_flux (shell),
                                     service);
    if (!t) {
        shell_log_errno ("flux_terminus_server_create");
        return NULL;
    }
    if (flux_shell_aux_set (shell,
                            "builtin::terminus",
                            t,
                            (flux_free_f) flux_terminus_server_destroy) < 0)
        return NULL;
    flux_terminus_server_set_log (t, shlog, NULL);

    /* Ensure process knows it is a terminus session */
    flux_shell_setenvf (shell, 1, "FLUX_TERMINUS_SESSION", "0");

    return t;
}

static int pty_init (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *arg)
{
    const char *shell_service;
    int shell_rank = -1;
    flux_shell_t *shell;
    struct flux_pty *pty;
    struct flux_terminus_server *t;

    if (!(shell = flux_plugin_get_shell (p)))
        return shell_log_errno ("flux_plugin_get_shell");

    if (flux_shell_info_unpack (shell,
                                "{s:i s:s}",
                                "rank", &shell_rank,
                                "service", &shell_service) < 0)
        return shell_log_errno ("flux_shell_info_unpack: service");

    if (!(t = shell_terminus_server_start (shell, shell_service)))
        return -1;

    /*  Only create a session for rank 0 if the pty option was specified
     */
    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    /* On rank 0, open a pty for task 0 only. It is important that the
     *   pty service be started before the shell.init event, since a client
     *   may attempt to attach to the pty immediately after this event.
     */
    if (shell_rank == 0) {
        pty = flux_terminus_server_session_open (t, 0, "task0");
        if (!pty)
            return shell_log_errno ("terminus_session_open");

        if (flux_shell_aux_set (shell, "builtin::pty.0", pty, NULL) < 0)
            goto error;

        if (flux_shell_add_event_context (shell,
                                          "shell.init",
                                          0,
                                          "{s:s}",
                                          "pty", "terminus.0") < 0) {
            shell_log_errno ("flux_shell_service_register");
            goto error;
        }
    }
    return 0;
error:
    flux_terminus_server_destroy (t);
    return -1;
}

static int pty_task_exec (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    int rank;

    if (!shell)
        return shell_log_errno ("failed to get shell object");

    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    if (!(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return shell_log_errno ("unable to get task rank");

    /*  pty on rank 0 only for now */
    if (rank == 0) {
        struct flux_pty *pty = flux_shell_aux_get (shell, "builtin::pty.0");

        /*  Redirect stdio to 'pty'
         */
        if (pty && flux_pty_attach (pty) < 0)
            return shell_log_errno ("pty attach failed");

        /*  Set environment variable so process knows it is running
         *   under a terminus server.
         */
        flux_shell_setenvf (shell, 1, "FLUX_TERMINUS_SESSION", "%d", rank);
    }
    return (0);
}

static int pty_task_exit (flux_plugin_t *p,
                          const char *topic,
                          flux_plugin_arg_t *args,
                          void *arg)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    flux_shell_task_t *task;
    int rank;

    if (!shell)
        return shell_log_errno ("failed to get shell object");

    if (flux_shell_getopt (shell, "pty", NULL) != 1)
        return 0;

    if (!(task = flux_shell_current_task (shell))
        || flux_shell_task_info_unpack (task, "{s:i}", "rank", &rank) < 0)
        return shell_log_errno ("unable to get task rank");

    /*  pty is present only on rank 0 only for now */
    if (rank == 0) {
        struct flux_terminus_server *t = NULL;
        struct flux_pty *pty;
        int status = flux_subprocess_status (flux_shell_task_subprocess (task));

        if (!(t = flux_shell_aux_get (shell, "builtin::terminus"))
            || !(pty = flux_shell_aux_get (shell, "builtin::pty.0")))
            return shell_log_errno ("failed to get terminus and pty objects");

        if (t && pty
            && flux_terminus_server_session_close (t, pty, status) < 0)
            shell_die_errno (1, "pty attach failed");
    }
    return (0);
}

struct shell_builtin builtin_pty = {
    .name = "pty",
    .init = pty_init,
    .task_exec = pty_task_exec,
    .task_exit = pty_task_exit,
};

/* vi: ts=4 sw=4 expandtab
 */
