/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exec_cmd.c - build the base job-shell command
 *
 * Shared by the exec implementations that launch real job shells (bulk-exec
 * and bgexec).  The per-rank sdexec push machinery remains private to each
 * implementation since it is coupled to the implementation's push interface.
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <flux/core.h>

#include "ccan/str/str.h"

#include "job-exec.h"
#include "exec_config.h"
#include "exec_cmd.h"

extern char **environ;

flux_cmd_t *job_shell_cmd_create (struct jobinfo *job, const char *service)
{
    flux_cmd_t *cmd;

    if (!(cmd = flux_cmd_create (0, NULL, environ))) {
        flux_log_error (job->h, "exec_init: flux_cmd_create");
        return NULL;
    }
    /* Set any configured exec.sdexec-properties.
     */
    json_t *props;
    if (streq (service, "sdexec")
        && (props = config_get_sdexec_properties ())) {
        const char *k;
        json_t *v;
        json_object_foreach (props, k, v) {
            char name[128];
            snprintf (name, sizeof (name), "SDEXEC_PROP_%s", k);
            if (flux_cmd_setopt (cmd, name, json_string_value (v)) < 0) {
                flux_log_error (job->h, "Unable to set sdexec options");
                goto err;
            }
        }
    }
    if (flux_cmd_setenvf (cmd, 1, "FLUX_KVS_NAMESPACE", "%s", job->ns) < 0) {
        flux_log_error (job->h, "exec_init: flux_cmd_setenvf");
        goto err;
    }
    if (job->multiuser) {
        if (flux_cmd_setenvf (cmd,
                              1,
                              "FLUX_IMP_EXEC_HELPER",
                              "flux imp_exec_helper %ju",
                              (uintmax_t) job->id) < 0) {
            flux_log_error (job->h, "exec_init: flux_cmd_setenvf");
            goto err;
        }
        /* The systemd user instance running as user flux is not privileged
         * to signal guest processes, therefore:
         * - Set the KillMode=process so only the IMP is signaled
         * - Use Type=notify in conjunction with IMP calling sd_notify(3) so
         *   the unit transitions to deactivating when the shell exits.
         * - Set TimeoutStopUsec=infinity to disable systemd's stop timeout.
         * - Enable sdexec's stop timeout which is armed at deactivating,
         *   delivers SIGUSR1 (proxy for SIGKILL) after 30s, then abandons
         *   the unit and terminates the exec RPC after another 30s.
         */
        if (streq (service, "sdexec")) {
            if (flux_cmd_setopt (cmd, "SDEXEC_PROP_KillMode", "process") < 0
                || flux_cmd_setopt (cmd, "SDEXEC_PROP_Type", "notify") < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_PROP_TimeoutStopUSec",
                                    "infinity") < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_STOP_TIMER_SIGNAL",
                                    config_get_sdexec_stop_timer_signal ()) < 0
                || flux_cmd_setopt (cmd,
                                    "SDEXEC_STOP_TIMER_SEC",
                                    config_get_sdexec_stop_timer_sec ()) < 0) {
                flux_log_error (job->h,
                                "Unable to set multiuser sdexec options");
                goto err;
            }
        }
        if (flux_cmd_argv_append (cmd, config_get_imp_path ()) < 0
            || flux_cmd_argv_append (cmd, "exec") < 0) {
            flux_log_error (job->h, "exec_init: flux_cmd_argv_append");
            goto err;
        }
    }
    if (flux_cmd_argv_append (cmd, config_get_job_shell (job)) < 0
        || flux_cmd_argv_appendf (cmd, "%ju", (uintmax_t) job->id) < 0) {
        flux_log_error (job->h, "exec_init: flux_cmd_argv_append");
        goto err;
    }
    return cmd;
err:
    flux_cmd_destroy (cmd);
    return NULL;
}

/* vi: ts=4 sw=4 expandtab
 */
