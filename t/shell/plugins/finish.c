/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#define FLUX_SHELL_PLUGIN_NAME "finish"

/* Test that delay in shell exit handler in follower shells
 * does not prevent leader shell from calling shell.exit handler
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/shell.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

static void get_shell_info (flux_shell_t *shell, int *rank, int *size)
{
    if (flux_shell_info_unpack (shell,
                                "{s:i s:i}",
                                "rank", rank,
                                "size", size))
        shell_die_errno (1, "flux_shell_info_unpack");
}

static int exit_cb (flux_plugin_t *p,
                    const char *topic,
                    flux_plugin_arg_t *args,
                    void *data)
{
    flux_future_t *f;
    flux_shell_t *shell = data;
    int shell_rank;
    int size;

    get_shell_info (shell, &shell_rank, &size);

    if (shell_rank == 0) {
        // On rank 0, ensure all 'test-finish' requests have arrived and
        // respond to them to release other ranks from shell.exit callback:
        zlistx_t *requests = flux_plugin_aux_get (p, "requests");
        flux_t *h = flux_shell_get_flux (shell);

        // Run reactor until all expected requests have arrived
        while (zlistx_size (requests) < size - 1)
            flux_reactor_run (flux_get_reactor (h), FLUX_REACTOR_ONCE);

        // Reply to all requests
        shell_log ("responding to %d requests", (int) zlistx_size (requests));
        const flux_msg_t *msg = zlistx_first (requests);
        while (msg) {
            flux_respond (h, msg, NULL);
            msg = zlistx_next (requests);
        }
        return 0;
    }

    // On follower shells, simply send a request to rank 0 and wait
    // for a response. If follower shell.exit callbacks prevent rank 0
    // job shell from exiting the reactor due to output plugin, then
    // this will hang the job since the leader shell only replies to
    // these write requests from its own shell.exit handler.
    shell_log ("sending test-finish request to rank 0");
    if (!(f = flux_shell_rpc_pack (shell, "test-finish", 0, 0, "{}"))
        || flux_future_get (f, NULL) < 0)
        return shell_log_errno ("failed to wait for test-finish response");
    return 0;
}

static void finish_service_cb (flux_t *h,
                               flux_msg_handler_t *mh,
                               const flux_msg_t *msg,
                               void *arg)
{
    zlistx_t *requests = arg;
    shell_log ("got request %d", (int) zlistx_size (requests) + 1);
    zlistx_add_end (requests, flux_msg_copy (msg, false));
}

static void requests_destroy (void *arg)
{
    zlistx_t *l = arg;
    flux_msg_t *msg;

    if (l) {
        msg = zlistx_first (l);
        while (msg) {
            flux_msg_decref (msg);
            msg = zlistx_next (l);
        }
        zlistx_destroy (&l);
    }
}

int flux_plugin_init (flux_plugin_t *p)
{
    flux_shell_t *shell = flux_plugin_get_shell (p);
    int shell_rank;
    int size;

     get_shell_info (shell, &shell_rank, &size);

    flux_plugin_set_name (p, FLUX_SHELL_PLUGIN_NAME);

    if (flux_plugin_add_handler (p, "shell.exit", exit_cb, shell) < 0)
        return -1;

    if (shell_rank == 0) {
        zlistx_t *requests;
        if (!(requests = zlistx_new ())
            || flux_shell_service_register (shell,
                                            "test-finish",
                                            finish_service_cb,
                                            requests) < 0
            || flux_plugin_aux_set (p,
                                    "requests",
                                    requests,
                                    requests_destroy) < 0)
            return -1;
    }
    return 0;
}

/*
 * vi: ts=4 sw=4 expandtab
 */
