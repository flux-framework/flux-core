/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell signal handling
 *
 * Setup disposition for signals in the shell.
 * Currently,
 *
 * SIGINT  - forward to all local tasks
 * SIGTERM - forward
 * SIGALRM - forward
 *
 */
#define FLUX_SHELL_PLUGIN_NAME "signals"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "internal.h"
#include "builtins.h"

static void signal_cb (flux_reactor_t *r, flux_watcher_t *w,
                       int revents, void *arg)
{
    flux_shell_t *shell = arg;
    int sig = flux_signal_watcher_get_signum (w);
    shell_debug ("forwarding signal %d to tasks", sig);
    flux_shell_killall (shell, sig);
}

static int trap_signal (flux_shell_t *shell, int signum)
{
    flux_watcher_t *w;

    if (!(w = flux_signal_watcher_create (shell->r, signum, signal_cb, shell)))
        return -1;
    flux_watcher_start (w);
    flux_aux_set (shell->h, NULL, w, (flux_free_f) flux_watcher_destroy);
    return 0;
}

static int signals_init (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!shell)
        return -1;
    /* forward local SIGINT, SIGTERM, SIGALRM to tasks */
    if (trap_signal (shell, SIGINT) < 0
        || trap_signal (shell, SIGTERM) < 0
        || trap_signal (shell, SIGALRM) < 0)
        shell_log_errno ("failed to set up signal watchers");
    return 0;
}

struct shell_builtin builtin_signals = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = signals_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
