/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "getopt"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

static int die (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    return -1;
}

static int check_getopt (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    int result;
    char *json_str;

    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!p)
        return die ("flux_plugin_get_shell\n");

    ok (flux_shell_getopt (shell, "noexist", NULL) == 0,
        "flux_shell_getopt of nonexistent option returns 0");
    ok (flux_shell_getopt (shell, "test", &json_str) == 1,
        "flux_shell_getopt returns 1 on success");
    ok (json_str != NULL,
        "flux_shell_getopt returned JSON string");
    diag ("getopt: %s: test: %s", topic, json_str);
    free (json_str);

    ok (flux_shell_getopt_unpack (shell, "noexist", "i", &result) == 0,
        "flux_shell_getopt_unpack of nonexistent option returns 0");
    ok (flux_shell_getopt_unpack (shell, "test", "i", &result) == 1,
        "flux_shell_getopt_unpack returns 1 on success");
    ok (flux_shell_getopt_unpack (shell, "test", "s", &json_str) < 0,
        "flux_shell_getopt_unpack returns -1 for bad unpack args");

    ok (flux_shell_setopt (shell, "new", "42") == 0,
        "flux_shell_setopt of new option works");
    ok (flux_shell_getopt_unpack (shell, "new", "i", &result) == 1
        && result == 42,
        "setopt worked and set integer value");

    ok (flux_shell_setopt (shell, "new", NULL) == 0,
        "flux_shell_setopt with NULL value worked");
    ok (flux_shell_getopt (shell, "new", NULL) == 0,
        "flux_shell_getopt shows that unset option worked");

    ok (flux_shell_setopt_pack (shell, "new", "i", 42) == 0,
        "flux_shell_setopt_pack worked");
    ok (flux_shell_getopt_unpack (shell, "new", "i", &result) == 1
        && result == 42,
        "setopt worked and set integer value");

    if (streq (topic, "shell.exit") || streq (topic, "task.exec"))
        return exit_status () == 0 ? 0 : -1;
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    plan (NO_PLAN);
    ok (flux_plugin_add_handler (p, "*", check_getopt, NULL) == 0,
        "flux_plugin_add_handler works");
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
