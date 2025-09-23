/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* msgchan.c - message channel
 *
 * This is intended to be encapsulated and tested on its own, then
 * integrated with libsubprocess.
 *
 * Server calls msgchan_create(), which
 * - opens a socketpair
 * - flux_open (fd://) of socketpair (server end)
 * - flux_open (relay URI)
 * - reactively copy, bidirectionally, between fd and relay handles
 *
 * The client:
 * - flux_opens (fd://) of socketpair (client end)
 * - does whatever
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"

#include "msgchan.h"

struct handle_stats {
    int sends;
    int recvs;
    int send_errors;
    int recv_errors;
    int requeue_errors;
    int stalls;
};

struct watched_handle {
    flux_t *h;
    flux_watcher_t *read_w;
    flux_watcher_t *write_w;
    struct handle_stats stats;
    struct watched_handle *peer;
};

struct msgchan {
    int sock[2];    // socketpair
    char fduri[2][32];
    char *relay_uri;// URI specified at creation
    struct watched_handle hfd; // handle opened to fd://sock[0]
    struct watched_handle h; // handle opened to creation uri
};

json_t *watched_handle_get_stats (struct watched_handle *wh)
{
    return json_pack ("{s:i s:i s:i s:i s:i s:i}",
                      "sends", wh->stats.sends,
                      "recvs", wh->stats.recvs,
                      "senderr", wh->stats.send_errors,
                      "recverr", wh->stats.recv_errors,
                      "rqerr", wh->stats.requeue_errors,
                      "stalls", wh->stats.stalls);
}

int msgchan_get_stats (struct msgchan *mch, json_t **op)
{
    json_t *h = watched_handle_get_stats (&mch->h);
    json_t *hfd = watched_handle_get_stats (&mch->hfd);
    json_t *o = NULL;
    if (h && hfd) {
        o = json_pack ("{s:O s:O}",
                       mch->relay_uri, h,
                       mch->fduri[0], hfd);
    }
    json_decref (h);
    json_decref (hfd);
    if (o) {
        *op = o;
        return 0;
    }
    return -1;
}

/* Handle is writable.
 * Stop the write watcher and start the peer read watcher.
 */
static void write_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    struct watched_handle *wh = arg;

    if (!(revents & FLUX_POLLOUT))
        return;
    if (!wh->peer) {
        flux_watcher_stop (w);
        return; // sanity
    }
    flux_watcher_start (wh->peer->read_w);
    flux_watcher_stop (wh->write_w);
}

/* Handle is readable.
 * Read a message and write it the peer.
 * If the peer is not writable, requeue the message,
 * then stop the read watcher and start the peer write watcher.
 */
static void read_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct watched_handle *wh = arg;
    flux_msg_t *msg;

    if (!(revents & FLUX_POLLIN))
        return;
    if (!wh->peer) {
        flux_watcher_stop (w);
        return; // sanity
    }
    if (!(msg = flux_recv (wh->h, FLUX_MATCH_ANY, FLUX_O_NONBLOCK))) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            wh->stats.recv_errors++;
            return;
        }
        return; // spurious wake-up
    }
    if (flux_send_new (wh->peer->h, &msg, FLUX_O_NONBLOCK) < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            wh->peer->stats.send_errors++;
            return;
        }
        if (flux_requeue (wh->h, msg, FLUX_RQ_HEAD) < 0) {
            wh->stats.requeue_errors++;
            flux_msg_decref (msg);
            return;
        }
        flux_msg_decref (msg); // requeue took a ref
        flux_watcher_stop (wh->read_w);
        flux_watcher_start (wh->peer->write_w);
        wh->peer->stats.stalls++;
    }
    else {
        wh->stats.recvs++;
        wh->peer->stats.sends++;
    }
}

static void watched_handle_close (struct watched_handle *wh)
{
    flux_watcher_destroy (wh->read_w);
    flux_watcher_destroy (wh->write_w);
    flux_close (wh->h);
}

static int watched_handle_open (struct watched_handle *wh,
                                const char *uri,
                                flux_reactor_t *reactor,
                                flux_error_t *error)
{
    if (!(wh->h = flux_open_ex (uri, FLUX_O_NONBLOCK, error))
        || flux_set_reactor (wh->h, reactor))
        return -1;
    if (!(wh->read_w = flux_handle_watcher_create (reactor,
                                                   wh->h,
                                                   FLUX_POLLIN,
                                                   read_cb,
                                                   wh)))
        goto error;
    if (!(wh->write_w = flux_handle_watcher_create (reactor,
                                                    wh->h,
                                                    FLUX_POLLOUT,
                                                    write_cb,
                                                    wh)))
        goto error;
    flux_watcher_start (wh->read_w);
    return 0;
error:
    errprintf (error, "error creating message watchers: %s", strerror (errno));
    return -1;
}

static void socketpair_init (int *fd)
{
    for (int i = 0; i < 2; i++)
        fd[i] = -1;
}

static void socketpair_close (int *fd)
{
    for (int i = 0; i < 2; i++) {
        if (fd[i] >= 0)
            (void)close (fd[i]);
    }
    socketpair_init (fd);
}

void msgchan_destroy (struct msgchan *mch)
{
    if (mch) {
        int saved_errno = errno;
        watched_handle_close (&mch->h);
        watched_handle_close (&mch->hfd);
        socketpair_close (mch->sock);
        free (mch->relay_uri);
        free (mch);
        errno = saved_errno;
    }
}

const char *msgchan_get_uri (struct msgchan *mch)
{
    return mch->fduri[1];
}

int msgchan_get_fd (struct msgchan *mch)
{
    return mch->sock[1];
}

static int write_zero (int fd)
{
    unsigned char e = 0;
    int n;
    if ((n = write (fd, &e, 1)) < 0)
        return -1;
    if (n == 0) {
        errno = ECONNRESET;
        return -1;
    }
    return 0;
}

struct msgchan *msgchan_create (flux_reactor_t *reactor,
                                const char *relay_uri,
                                flux_error_t *error)
{
    struct msgchan *mch;

    if (!reactor || !relay_uri) {
        errno = EINVAL;
        errprintf (error, "invalid arguments");
        return NULL;
    }
    if (!(mch = calloc (1, sizeof (*mch)))
        || !(mch->relay_uri = strdup (relay_uri))) {
        errprintf (error, "out of memory");
        goto error;
    }
    socketpair_init (mch->sock);
    if (socketpair (PF_LOCAL, SOCK_STREAM, 0, mch->sock) < 0) {
        errprintf (error, "socketpair: %s", strerror (errno));
        goto error;
    }
    /* fd:// shares usock_client code with the local connector, and
     * usock_client_connect() synchronously reads an "auth byte" when
     * the handle is opened.  Since these fd:// connections are back to back,
     * queue that up ahead of time in both directions (0=auth success).
     */
    if (write_zero (mch->sock[0]) < 0 || write_zero (mch->sock[1]) < 0) {
        errprintf (error, "write to socketpair: %s", strerror (errno));
        goto error;
    }
    snprintf (mch->fduri[0], sizeof (mch->fduri[0]), "fd://%d", mch->sock[0]);
    snprintf (mch->fduri[1], sizeof (mch->fduri[1]), "fd://%d", mch->sock[1]);
    if (watched_handle_open (&mch->h, mch->relay_uri, reactor, error) < 0
        || watched_handle_open (&mch->hfd, mch->fduri[0], reactor, error) < 0)
        goto error;
    mch->hfd.peer = &mch->h;
    mch->h.peer = &mch->hfd;
    return mch;
error:
    msgchan_destroy (mch);
    return NULL;
}

// vi: ts=4 sw=4 expandtab
