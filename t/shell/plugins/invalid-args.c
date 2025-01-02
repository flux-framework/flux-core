/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/
#define FLUX_SHELL_PLUGIN_NAME "invalid-args"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
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

static int shell_cb (flux_plugin_t *p,
                     const char *topic,
                     flux_plugin_arg_t *args,
                     void *data)
{
    char *json_str;
    flux_shell_t *shell = flux_plugin_get_shell (p);

    if (streq (topic, "shell.log"))
        return 0;

    diag ("invalid-args: %s", topic);
    if (!shell)
        return die ("flux_plugin_get_shell: %s\n", strerror (errno));

    ok (flux_shell_aux_set (NULL, "topic", NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_aux_set (NULL, ...) returns EINVAL");
    ok (flux_shell_aux_set (shell, NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_aux_set (shell, NULL, ...) returns EINVAL");

    ok (!flux_shell_aux_get (NULL, "topic") && errno == EINVAL,
        "flux_shell_aux_get (NULL, ...) returns EINVAL");
    ok (!flux_shell_aux_get (shell, NULL) && errno == EINVAL,
        "flux_shell_aux_get (shell, NULL) returns EINVAL");

    ok (flux_shell_getopt (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_getopt with NULL args returns EINVAL");
    ok (flux_shell_getopt (shell, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_getopt with NULL name returns EINVAL");
    ok (flux_shell_getopt (shell, "foo", NULL) == 0,
        "flux_shell_getopt no opt returns 0");

    ok (flux_shell_getopt_unpack (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_getopt_unpack with NULL args returns EINVAL");
    ok (flux_shell_getopt_unpack (shell, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_getopt_unpack with NULL name returns EINVAL");
    ok (flux_shell_getopt_unpack (shell, "foo", NULL) == 0,
        "flux_shell_getopt_unpack no opt returns 0");

    ok (flux_shell_getenv (NULL, "foo") == NULL && errno == EINVAL,
        "flux_shell_getenv (NULL, 'foo') returns EINVAL");
    ok (flux_shell_getenv (shell, NULL) == NULL && errno == EINVAL,
        "flux_shell_getenv (shell, NULL) returns EINVAL");

    ok (flux_shell_unsetenv (NULL, "foo") < 0 && errno == EINVAL,
        "flux_shell_unsetenv (NULL, 'foo') returns EINVAL");
    ok (flux_shell_unsetenv (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_unsetenv (shell, NULL) returns EINVAL");
    ok (flux_shell_unsetenv (shell, "MissingEnvVar") < 0 && errno == ENOENT,
        "flux_shell_unsetenv (shell, MissingEnvVar) returns ENOENT");

    ok (flux_shell_setenvf (NULL, 0, "foo", "bar") < 0 && errno == EINVAL,
        "flux_shell_setenvf (NULL, ...) returns EINVAL");
    ok (flux_shell_setenvf (shell, 0, NULL, "bar") < 0 && errno == EINVAL,
        "flux_shell_setenvf NULL key returns EINVAL");
    ok (flux_shell_setenvf (shell, 0, "foo", NULL) < 0 && errno == EINVAL,
        "flux_shell_setenvf NULL val returns EINVAL");

    ok (flux_shell_get_environ (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_environ with NULL args returns EINVAL");
    ok (flux_shell_get_environ (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_environ with NULL json_str returns EINVAL");

    ok (flux_shell_get_hwloc_xml (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_hwloc_xml with NULL args returns EINVAL");
    ok (flux_shell_get_hwloc_xml (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_hwloc_xml with NULL xml pointer returns EINVAL");

    ok (flux_shell_get_info (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_info with NUll arg returns EINVAL");
    ok (flux_shell_get_info (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_info with NUll json_str returns EINVAL");

    ok (flux_shell_info_unpack (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_info_unpack with NUll arg returns EINVAL");
    ok (flux_shell_info_unpack (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_info_unpack with NULL fmt returns EINVAL");

    ok (flux_shell_get_jobspec_info (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_info with NULL arg returns EINVAL");
    ok (flux_shell_get_jobspec_info (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_info with NULL json_str returns EINVAL");

    ok (flux_shell_get_taskmap (NULL) == NULL && errno == EINVAL,
        "flux_shell_get_taskmap (NULL) returns EINVAL");

    ok (flux_shell_jobspec_info_unpack (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_info_unpack with NULL arg returns EINVAL");
    ok (flux_shell_jobspec_info_unpack (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_info_unpack with NULL fmt returns EINVAL");

    ok (flux_shell_get_rank_info (NULL, -1, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_rank_info (NULL, ..) returns EINVAL");
    ok (flux_shell_get_rank_info (shell, -1, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_rank_info (NULL, ..) returns EINVAL");

    ok (flux_shell_get_rank_info (shell, -1, NULL) < 0 && errno == EINVAL,
        "flux_shell_get_rank_info (NULL, ..) returns EINVAL");
    ok (flux_shell_get_rank_info (shell, 12, &json_str) < 0 && errno == EINVAL,
        "flux_shell_get_rank_info with invalid rank returns EINVAL");
    ok (flux_shell_get_rank_info (shell, -2, &json_str) < 0 && errno == EINVAL,
        "flux_shell_get_rank_info with rank < -1 returns EINVAL");

    ok (flux_shell_rank_info_unpack (NULL, -1, NULL) < 0 && errno == EINVAL,
        "flux_shell_rank_info_unpack (NULL, ..) returns EINVAL");
    ok (flux_shell_get_rank_info (shell, -1, NULL) < 0 && errno == EINVAL,
        "flux_shell_rank_info_unpack (NULL, ..) returns EINVAL");

    int n;
    ok (flux_shell_rank_info_unpack (shell, 12, "{s:i}", "ntasks", &n) < 0
        && errno == EINVAL,
        "flux_shell_rank_info_unpack with invalid rank returns EINVAL");
    ok (flux_shell_rank_info_unpack (shell, -2, "{s:i}", "ntasks", &n) < 0
        && errno == EINVAL,
        "flux_shell_rank_info_unpack with rank < -1 returns EINVAL");

    ok (flux_shell_add_event_handler (NULL, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_add_event_handler (NULL, ...) returns EINVAL");
    ok (flux_shell_add_event_handler (shell, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_add_event_handler (shell, NULL, ...) returns EINVAL");
    ok (flux_shell_add_event_handler (shell, "foo", NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_add_event_handler (shell, NULL, ...) returns EINVAL");

    ok (flux_shell_service_register (NULL, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_service_register (NULL, ...) returns EINVAL");
    ok (flux_shell_service_register (shell, "method", NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_service_register (shell, str, NULL) returns EINVAL");

    ok (!flux_shell_rpc_pack (NULL, NULL, 0, 0, NULL) && errno == EINVAL,
        "flux_shell_rpc_pack with NULL args returns EINVAL");
    ok (!flux_shell_rpc_pack (shell, NULL, 0, 0, NULL) && errno == EINVAL,
        "flux_shell_rpc_pack with NULL method returns EINVAL");

    ok (flux_shell_plugstack_call (NULL, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_plugstack_call with NULL args returns EINVAL");
    ok (flux_shell_plugstack_call (shell, NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_plugstack_call with NULL topic returns EINVAL");

    ok (flux_shell_add_completion_ref (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_add_completion_ref with NULL args returns EINVAL");
    ok (flux_shell_add_completion_ref (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_add_completion_ref with NULL name returns EINVAL");

    ok (flux_shell_remove_completion_ref (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_remove_completion_ref with NULL args returns EINVAL");
    ok (flux_shell_remove_completion_ref (shell, NULL) < 0 && errno == EINVAL,
        "flux_shell_remove_completion_ref with NULL name returns EINVAL");

    ok (flux_shell_add_event_context (NULL, NULL, 0, NULL) < 0
        && errno == EINVAL,
        "flux_shell_add_event_context with NULL args returns EINVAL");
    ok (flux_shell_add_event_context (shell, NULL, 0, "{}") < 0
        && errno == EINVAL,
        "flux_shell_add_event_context with NULL name returns EINVAL");
    ok (flux_shell_add_event_context (shell, "main", 0, NULL) < 0
        && errno == EINVAL,
        "flux_shell_add_event_context with NULL fmt returns EINVAL");

    ok (flux_shell_task_first (NULL) == NULL && errno == EINVAL,
        "flux_shell_task_first (NULL) returns EINVAL");
    ok (flux_shell_task_next (NULL) == NULL && errno == EINVAL,
        "flux_shell_task_next (NULL) returns EINVAL");

    ok (flux_shell_mustache_render (NULL, NULL) == NULL && errno == EINVAL,
        "flux_shell_mustache_render (NULL, NULL) returns EINVAL");
    ok (flux_shell_mustache_render (shell, NULL) == NULL && errno == EINVAL,
        "flux_shell_mustache_render (shell, NULL) returns EINVAL");

    if (streq (topic, "shell.init")) {
        ok (flux_shell_current_task (NULL) == NULL && errno == EINVAL,
            "flux_shell_current_task with NULL shell returns EINVAL");
        errno = 0;
        ok (flux_shell_current_task (shell) == NULL && errno == 0,
            "flux_shell_current_task returns no task in shell.init");
    }
    if (streq (topic, "shell.exit"))
        return exit_status () == 0 ? 0 : -1;
    return 0;
}

static int task_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *data)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!shell)
        die ("flux_plugin_get_shell()");
    flux_shell_task_t *task = flux_shell_current_task (shell);
    if (!task)
        die ("flux_shell_current_task()");

    ok (flux_shell_task_get_info (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_task_get_info with NULL args returns EINVAL");
    ok (flux_shell_task_get_info (task, NULL) < 0 && errno == EINVAL,
        "flux_shell_task_get_info with NULL task returns EINVAL");

    ok (flux_shell_task_info_unpack (NULL, NULL) < 0 && errno == EINVAL,
        "flux_shell_task_info_unpack with NULL args returns EINVAL");
    ok (flux_shell_task_info_unpack (task, NULL) < 0 && errno == EINVAL,
        "flux_shell_task_info_unpack with NULL fmt returns EINVAL");

    ok (!flux_shell_task_subprocess (NULL) && errno == EINVAL,
        "flux_shell_task_subprocess with NULL task returns EINVAL");

    ok (!flux_shell_task_cmd (NULL) && errno == EINVAL,
        "flux_shell_task_cmd with NULL task returns EINVAL");

    ok (flux_shell_task_channel_subscribe (NULL, NULL, NULL, NULL) < 0
        && errno == EINVAL,
        "flux_shell_task_channel_subscribe with NULL args returns EINVAL");

    if (streq (topic, "task.exec"))
        return exit_status () == 0 ? 0 : -1;
    return 0;
}

int flux_plugin_init (flux_plugin_t *p)
{
    plan (NO_PLAN);
    ok (flux_plugin_add_handler (p, "shell.*", shell_cb, NULL) == 0,
        "flux_plugin_add_handler works");
    ok (flux_plugin_add_handler (p, "task.*", task_cb, NULL) == 0,
        "flux_plugin_add_handler works");
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
