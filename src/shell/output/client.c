/************************************************************\
 * Copyright 2024 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* std output leader service client
 *
 * When output is to the KVS or a single output file, non-leader
 * shell ranks send output and log data to the rank 0 shell via
 * RPCs.
 *
 * Notes:
 *  - Errors from write requests to leader shell are logged.
 *  - Outstanding RPCs at shell exit are waited for synchronously.
 *  - Number of in-flight write RPCs is limited by shell_output_hwm
 *    to avoid matchtag exhaustion.
 */
#if HAVE_CONFIG_H
#include "config.h"
#endif

/* Note: necessary for shell_log functions
 */
#define FLUX_SHELL_PLUGIN_NAME "output.client"

#include "src/common/libioencode/ioencode.h"
#include "ccan/str/str.h"

#include "internal.h"
#include "info.h"
#include "output/client.h"

static const int shell_output_lwm = 100;
static const int shell_output_hwm = 1000;

struct output_client {
    flux_shell_t *shell;
    int shell_rank;
    bool stopped;
    zlist_t *pending_writes;
};

static void client_send_eof (struct output_client *client)
{
    /* Note: client should not be instantiated on rank 0, but check here
     * just in case.
     */
    if (client->shell_rank != 0) {
        flux_future_t *f;
        /* Nonzero shell rank: send EOF to leader shell to notify
         *  that no more messages will be sent to shell.write
         */
        if (!(f = flux_shell_rpc_pack (client->shell,
                                       "write",
                                        0,
                                        0,
                                        "{s:s s:i s:{}}",
                                        "name", "eof",
                                        "shell_rank", client->shell_rank,
                                        "context")))
            shell_log_errno ("shell.write: eof");
        flux_future_destroy (f);
    }
}

void output_client_destroy (struct output_client *client)
{
    if (client) {
        int saved_errno = errno;

        client_send_eof (client);

        if (client->pending_writes) {
            flux_future_t *f;
            while ((f = zlist_pop (client->pending_writes))) {
                if (flux_future_get (f, NULL) < 0 && errno != ENOSYS)
                    shell_log_errno ("client write failed");
                flux_future_destroy (f);
            }
        }
        zlist_destroy (&client->pending_writes);
        free (client);
        errno = saved_errno;
    }
}

struct output_client *output_client_create (flux_shell_t *shell)
{
    struct output_client *client;
    if (!(client = calloc (1, sizeof (*client)))
        || !(client->pending_writes = zlist_new ()))
        goto out;
    client->shell = shell;
    client->shell_rank = shell->info->shell_rank;
    return client;
out:
    output_client_destroy (client);
    return NULL;

}

/* Pause/resume output for all local tasks
 */
static void output_client_control (struct output_client *client, bool stop)
{
    if (client->stopped != stop) {
        flux_shell_task_t *task = flux_shell_task_first (client->shell);
        while (task) {
            flux_subprocess_t *p = flux_shell_task_subprocess (task);

            if (stop) {
                flux_subprocess_stream_stop (p, "stdout");
                flux_subprocess_stream_stop (p, "stdout");
            }
            else {
                flux_subprocess_stream_start (p, "stdout");
                flux_subprocess_stream_start (p, "stdout");
            }
            task = flux_shell_task_next (client->shell);
        }
    }
}

static void output_send_cb (flux_future_t *f, void *arg)
{
    struct output_client *client = arg;
    if (flux_future_get (f, NULL) < 0 && errno != ENOSYS)
        shell_log_errno ("error writing output to leader");
    zlist_remove (client->pending_writes, f);
    flux_future_destroy (f);

    if (zlist_size (client->pending_writes) <= shell_output_lwm)
        output_client_control (client, false);
}

int output_client_send (struct output_client *client,
                        const char *type,
                        json_t *context)
{
    flux_future_t *f = NULL;

    if (!(f = flux_shell_rpc_pack (client->shell,
                                   "write",
                                   0,
                                   0,
                                   "{s:s s:i s:O}",
                                   "name", type,
                                   "shell_rank", client->shell_rank,
                                   "context", context))
        || flux_future_then (f, -1., output_send_cb, client) < 0)
        goto error;
    if (zlist_append (client->pending_writes, f) < 0)
        shell_log_error ("failed to append pending write");
    if (zlist_size (client->pending_writes) >= shell_output_hwm)
        output_client_control (client, true);
    return 0;
error:
    flux_future_destroy (f);
    return -1;

}

/* vi: ts=4 sw=4 expandtab
 */
