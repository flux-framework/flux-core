/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "setopt"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include <jansson.h>
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libtap/tap.h"

static int die (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    vfprintf (stderr, fmt, ap);
    va_end (ap);
    return -1;
}

static int check_setopt (flux_plugin_t *p,
                         const char *topic,
                         flux_plugin_arg_t *args,
                         void *data)
{
    json_t *options = NULL;

    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!p)
        return die ("flux_plugin_get_shell\n");

    ok (flux_shell_info_unpack (shell,
                                "{s:{s:{s:{s:{s:o}}}}}",
                                "jobspec",
                                "attributes",
                                "system",
                                "shell",
                                "options", &options) < 0,
        "flux_shell_info_unpack shell options fails");

    /*  A shell plugin should be able to call setopt even though
     *   no shell options were currently set in jobspec.
     */
    ok (flux_shell_setopt (shell, "new", "42") == 0,
        "flux_shell_setopt of new option works");

    return exit_status () == 0 ? 0 : -1;
}

int flux_plugin_init (flux_plugin_t *p)
{
    plan (NO_PLAN);
    ok (flux_plugin_add_handler (p, "shell.init", check_setopt, NULL) == 0,
        "flux_plugin_add_handler works");
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
