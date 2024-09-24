/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* shell plugin handling direct notification of job exceptions
 *
 */
#define FLUX_SHELL_PLUGIN_NAME "exception"

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/shell.h>

#include "ccan/str/str.h"
#include "src/common/libeventlog/eventlog.h"

#include "internal.h"
#include "builtins.h"

static void exception_handler (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    flux_shell_t *shell = arg;
    const char *type;
    int severity = -1;
    int shell_rank = -1;
    const char *message = "";

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:s s:i s:i s?s}",
                             "type", &type,
                             "severity", &severity,
                             "shell_rank", &shell_rank,
                             "message", &message) < 0)
        goto error;

    if (strlen (message) > 0)
        shell_warn ("%s", message);

    if (streq (type, "lost-shell")) {
        flux_plugin_arg_t *args = flux_plugin_arg_create ();
        if (!args
            || flux_plugin_arg_pack (args,
                                     FLUX_PLUGIN_ARG_IN,
                                     "{s:i s:i}",
                                     "shell_rank", shell_rank,
                                     "severity", severity) < 0) {
            flux_plugin_arg_destroy (args);
            goto error;
        }
        flux_shell_plugstack_call (shell, "shell.lost", args);
        flux_plugin_arg_destroy (args);
    }

    if (flux_respond (h, msg, NULL) < 0)
        shell_log_errno ("flux_respond");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        shell_log_errno ("flux_respond_error");

}

static int exception_init (flux_plugin_t *p,
                           const char *topic,
                           flux_plugin_arg_t *args,
                           void *data)
{
    flux_t *h;
    flux_jobid_t id;
    int shell_rank;
    flux_shell_t *shell = flux_plugin_get_shell (p);
    if (!shell)
        return -1;
    if (!(h = flux_shell_get_flux (shell)))
        return -1;
    if (flux_shell_info_unpack (shell,
                                "{s:I s:i}",
                                "jobid", &id,
                                "rank", &shell_rank) < 0)
        return -1;
    if (shell_rank != 0)
        return 0;

    if (flux_shell_service_register (shell,
                                     "exception",
                                     exception_handler,
                                     shell) < 0)
        return -1;
    return 0;
}

struct shell_builtin builtin_exception = {
    .name = FLUX_SHELL_PLUGIN_NAME,
    .init = exception_init,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
