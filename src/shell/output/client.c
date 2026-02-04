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
 * RPCs using FLUX_RPC_NORESPONSE.
 *
 * A credit-based flow control protocol limits the number of
 * in-flight write requests: each shell starts with credits equal
 * to client->hwm, and requests more credits from the leader when
 * credits drop to client->lwm. If credits reach zero, the shell
 * output plugin stops reading output from local tasks until more
 * credits are received from the leader.
 *
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

struct output_client {
    flux_shell_t *shell;
    int shell_rank;
    bool stopped;
    int lwm;
    int hwm;
    int credits;
    flux_future_t *f_getcredit;
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
                                        FLUX_RPC_NORESPONSE,
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

        flux_future_destroy (client->f_getcredit);
        free (client);
        errno = saved_errno;
    }
}

struct output_client *output_client_create (flux_shell_t *shell,
                                            int client_lwm,
                                            int client_hwm)
{
    struct output_client *client;

    if (!(client = calloc (1, sizeof (*client))))
        return NULL;
    client->shell = shell;
    client->shell_rank = shell->info->shell_rank;
    client->lwm = client_lwm;
    client->hwm = client_hwm;
    client->credits = client_hwm;
    return client;

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
                flux_subprocess_stream_stop (p, "stderr");
            }
            else {
                flux_subprocess_stream_start (p, "stdout");
                flux_subprocess_stream_start (p, "stderr");
            }
            task = flux_shell_task_next (client->shell);
        }
        client->stopped = stop;
        shell_debug ("flow control %s", stop ? "stop" : "start");
    }
}

/* No more credit at the liquor store
 * Suit is all dirty, my shoes is all wore
 * Tired and lonely, my heart is all sore.  --Frank Zappa
 */
static void getcredit_continuation (flux_future_t *f, void *arg)
{
    struct output_client *client = arg;
    int credits;

    if (flux_rpc_get_unpack (f, "{s:i}", "credits", &credits) < 0) {
        shell_log_errno ("output is stopped and getcredit failed");
        return;
    }

    output_client_control (client, false);
    client->credits += credits;

    flux_future_destroy (f);
    client->f_getcredit = NULL;
}

int output_client_send (struct output_client *client,
                        const char *type,
                        json_t *context)
{
    flux_future_t *f;

    if (!(f = flux_shell_rpc_pack (client->shell,
                                   "write",
                                   0,
                                   FLUX_RPC_NORESPONSE,
                                   "{s:s s:i s:O}",
                                   "name", type,
                                   "shell_rank", client->shell_rank,
                                   "context", context)))
        return -1;
    flux_future_destroy (f);
    /* Order more credits at low water mark.
     */
    if (--client->credits <= client->lwm && !client->f_getcredit) {
        if (!(f = flux_shell_rpc_pack (client->shell,
                                       "write-getcredit",
                                       0,
                                       0,
                                       "{s:i}",
                                       "credits", client->hwm))
            || flux_future_then (f, -1, getcredit_continuation, client)) {
            shell_log_errno ("error requesting credit");
            goto error;
        }
        client->f_getcredit = f;
    }
    /* Stop output when credits are at zero
     */
    if (client->credits == 0)
        output_client_control (client, true);
    return 0;
error:
    flux_future_destroy (f);
    return -1;

}

/* vi: ts=4 sw=4 expandtab
 */
