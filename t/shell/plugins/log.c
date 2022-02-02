/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "log"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libtap/tap.h"

static int get_shell_rank (flux_shell_t *shell)
{
    int rank = -1;
    char *json_str = NULL;
    json_t *o = NULL;

    if (flux_shell_get_info (shell, &json_str) < 0
        || !(o = json_loads (json_str, 0, NULL))
        || json_unpack (o, "{s:i}", "rank", &rank) < 0)
        shell_log_errno ("failed to get shell rank");
    json_decref (o);
    free (json_str);
    return rank;
}

static int check_shell_log (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    int shell_rank;
    const char *s = NULL;

    if (strcmp (topic, "shell.log") == 0)
        return 0;

    shell_rank = get_shell_rank (shell);
    if (flux_shell_getopt_unpack (shell, "log-fatal-error", "s", &s) < 0)
        shell_die (1, "error parsing log-fatal-error");

    if (s) {
        if (strcmp (s, topic) == 0 && shell_rank == 1)
            shell_die (1, "log-fatal-error requested!");
        /* For log-fatal-error test, avoid the remaining log testing
         *  below on the non-fatal ranks
         */
        return 0;
    }

    shell_trace ("%s: trace message", topic);
    shell_debug ("%s: debug message", topic);
    shell_log ("%s: log message", topic);
    shell_warn ("%s: warn message", topic);
    shell_log_error ("%s: error message", topic);
    ok (shell_log_errn (EPERM, "%s: log_errn message", topic) == -1
        && errno == EPERM,
        "shell_log_errn (errnum, ...) sets errno and returns < 0");

    errno = EINVAL;
    ok (shell_log_errno ("%s: log_errno message", topic) == -1,
        "shell_log_errno returns -1");

    if (strcmp (topic, "shell.exit") == 0 || strcmp (topic, "task.exec") == 0)
        return exit_status () == 0 ? 0 : -1;
    return 0;
}

static void destructor (void *arg)
{
    shell_log_error ("destructor: using log from plugin destructor works");
}

int flux_plugin_init (flux_plugin_t *p)
{
    plan (NO_PLAN);
    flux_plugin_set_name (p, FLUX_SHELL_PLUGIN_NAME);

    /*  Set a dummy aux item to force our destructor to be called */
    flux_plugin_aux_set (p, NULL, p, destructor);

    ok (flux_plugin_add_handler (p, "*", check_shell_log, NULL) == 0,
        "flux_plugin_add_handler works");

    ok (flux_shell_log_setlevel (-2, NULL) < 0 && errno == EINVAL,
        "flux_shell_log_setlevel with invalid level fails");
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
