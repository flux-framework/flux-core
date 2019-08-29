/************************************************************  \
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libtap/tap.h"
#include "src/common/libutil/unlink_recursive.h"
#include "src/common/librouter/usock.h"

#include "usock_util.h"

/* Client wrapper with async send/recv support.
 */
struct cli {
    zlist_t *queue; // async send queue
    int fd;
    flux_watcher_t *outw;
    flux_watcher_t *inw;
    struct usock_client *client;
    cli_recv_f recv_cb;
    void *recv_arg;
};

/* Append message to send queue and wake up write watcher
 */
int cli_send (struct cli *cli, const flux_msg_t *msg)
{
    if (zlist_append (cli->queue, (flux_msg_t *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errno = ENOMEM;
        return -1;
    }
    flux_watcher_start (cli->outw);
    return 0;
}

/* Client is ready for reading.  Try to recv a message.  If a full message
 * is read, call the user's recv callback.  Otherwise, go back to sleep.
 */
static void cli_recv_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct cli *cli = arg;

    if ((revents & FLUX_POLLERR))
        BAIL_OUT ("cli_recv_cb POLLERR");
    if ((revents & FLUX_POLLIN)) {
        flux_msg_t *msg;
        if (!(msg = usock_client_recv (cli->client, FLUX_O_NONBLOCK))) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                BAIL_OUT ("usock_client_recv failed: %s",
                          flux_strerror (errno));
        }
        else {
            cli->recv_cb (cli, msg, cli->recv_arg);
            flux_msg_destroy (msg);
        }
    }
}

/* Client is ready for writing.  Try to send message at top of queue.
 * If a full message is sent, pop & destroy, and stop watcher if queue
 * is now empty.  If a partial message is read, go back to sleep.
 */
static void cli_send_cb (flux_reactor_t *r,
                         flux_watcher_t *w,
                         int revents,
                         void *arg)
{
    struct cli *cli = arg;

    if ((revents & FLUX_POLLERR))
        BAIL_OUT ("cli_send_cb POLLERR");
    if ((revents & FLUX_POLLOUT)) {
        flux_msg_t *msg = zlist_first (cli->queue);
        if (usock_client_send (cli->client, msg, FLUX_O_NONBLOCK) < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                BAIL_OUT ("usock_client_send failed: %s",
                          flux_strerror (errno));
        }
        else {
            msg = zlist_pop (cli->queue);
            flux_msg_decref (msg);
            if (zlist_size (cli->queue) == 0)
                flux_watcher_stop (cli->outw);
        }
    }
}

void cli_destroy (struct cli *cli)
{
    if (cli) {
        int saved_errno = errno;
        if (cli->queue) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (cli->queue)))
                flux_msg_destroy (msg);
            zlist_destroy (&cli->queue);
        }
        flux_watcher_destroy (cli->inw);
        flux_watcher_destroy (cli->outw);
        usock_client_destroy (cli->client);
        free (cli);
        errno = saved_errno;
    }
}

struct cli *cli_create (flux_reactor_t *r,
                        int fd,
                        cli_recv_f recv_cb,
                        void *arg)
{
    struct cli *cli;
    if (!(cli = calloc (1, sizeof (*cli))))
        return NULL;
    if (!(cli->queue = zlist_new ()))
        goto error;
    cli->fd = fd;
    cli->recv_cb = recv_cb;
    cli->recv_arg = arg;
    if (!(cli->client = usock_client_create (fd)))
        goto error;
    if (!(cli->inw = flux_fd_watcher_create (r,
                                             usock_client_pollfd (cli->client),
                                             FLUX_POLLIN,
                                             cli_recv_cb,
                                             cli)))
        goto error;
    flux_watcher_start (cli->inw);
    if (!(cli->outw = flux_fd_watcher_create (r,
                                              usock_client_pollfd (cli->client),
                                              FLUX_POLLOUT,
                                              cli_send_cb,
                                              cli)))
        goto error;
    /* N.B. outw is made active only when queue has messages to send */
    return cli;
error:
    cli_destroy (cli);
    return NULL;
}

