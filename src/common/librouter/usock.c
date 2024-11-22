/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* usock.c - send/receive flux_msg_t's over local socket with reactor
 *
 * Accepting connections:
 * - Register "acceptor" callback to get a new 'struct usock_conn'
 *   when a new client connects.
 * - Use usock_conn_get_cred() to get SO_PEERCRED uid of peer.
 * - To accept, call usock_conn_accept().
 * - To reject, call usock_conn_reject(), then usock_conn_destroy().
 * - Error callback will be made on client disconnect (user destroys conn).
 * - Any remaining active connections are destroyed when server is destroyed.
 *
 * Pre-wired connection:
 * - It's possible to create a client connection directly from file
 *   descriptors using usock_conn_create()
 * - usock_conn_accept() must be called to set creds
 * - getsockopt(SO_PEERCRED) is skipped
 * - fd will not be closed when the connection is destroyed
 * - Use case: flux-relay "client" on stdin, stdout (tunneled through ssh)
 *
 * Sending/receiving messages from client:
 * - usock_conn_send() adds a message to a queue, starts fd (write) watcher.
 * - Register a receive callback to receive complete messages from client.
 * - Register an error callback to be notified when I/O errors occur.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <uuid.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/aux.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libutil/errno_safe.h"

#include "usock.h"
#include "sendfd.h"

#define LISTEN_BACKLOG 5

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37     // defined in later libuuid headers
#endif


struct usock_server {
    int fd;
    char *sockpath;
    flux_watcher_t *w;
    zlist_t *connections;
    usock_acceptor_f acceptor;
    void *arg;
};

struct usock_io {
    int fd;
    flux_watcher_t *w;
    struct iobuf iobuf;
};

struct usock_conn {
    struct flux_msg_cred cred;
    struct usock_io in;
    struct usock_io out;
    zlist_t *outqueue;

    usock_conn_close_f close_cb;
    void *close_arg;

    usock_conn_error_f error_cb;
    void *error_arg;

    usock_conn_recv_f recv_cb;
    void *recv_arg;

    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    struct aux_item *aux;
    struct usock_server *server;
    int refcount;

    unsigned char enable_close_on_destroy:1;
};

struct usock_client {
    int fd;
    struct iobuf in_iobuf;
    struct iobuf out_iobuf;
};

const struct flux_msg_cred *usock_conn_get_cred (struct usock_conn *conn)
{
    return conn ? &conn->cred : NULL;
}

const char *usock_conn_get_uuid (struct usock_conn *conn)
{
    return conn ? conn->uuid_str : NULL;
}

void usock_conn_set_error_cb (struct usock_conn *conn,
                              usock_conn_error_f cb,
                              void *arg)
{
    if (conn) {
        conn->error_cb = cb;
        conn->error_arg = arg;
    }
}

void usock_conn_set_close_cb (struct usock_conn *conn,
                              usock_conn_close_f cb,
                              void *arg)
{
    if (conn) {
        conn->close_cb = cb;
        conn->close_arg = arg;
    }
}


void usock_conn_set_recv_cb (struct usock_conn *conn,
                             usock_conn_recv_f cb,
                             void *arg)
{
    if (conn) {
        conn->recv_cb = cb;
        conn->recv_arg = arg;
    }
}

void *usock_conn_aux_get (struct usock_conn *conn, const char *name)
{
    if (!conn) {
        errno = EINVAL;
        return NULL;
    }
    return aux_get (conn->aux, name);
}

int usock_conn_aux_set (struct usock_conn *conn,
                        const char *name,
                        void *aux,
                        flux_free_f destroy)
{
    if (!conn) {
        errno = EINVAL;
        return -1;
    }
    return aux_set (&conn->aux, name, aux, destroy);
}

static void conn_io_error (struct usock_conn *conn, int errnum)
{
    if (conn) {
        if (conn->error_cb)
            conn->error_cb (conn, errnum, conn->error_arg);
        else
            usock_conn_destroy (conn);
    }
}

int usock_conn_send (struct usock_conn *conn, const flux_msg_t *msg)
{
    if (!conn || !msg) {
        errno = EINVAL;
        return -1;
    }
    if (zlist_append (conn->outqueue, (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errno = ENOMEM;
        return -1;
    }
    flux_watcher_start (conn->out.w);
    return 0;
}

static void conn_read_cb (flux_reactor_t *r,
                          flux_watcher_t *w,
                          int revents,
                          void *arg)
{
    struct usock_conn *conn = arg;

    if ((revents & FLUX_POLLERR)) {
        errno = EIO;
        goto error;
    }
    if ((revents & FLUX_POLLIN)) {
        flux_msg_t *msg;

        if (!(msg = recvfd (conn->in.fd, &conn->in.iobuf))) {
            if (errno != EWOULDBLOCK && errno != EAGAIN)
                goto error;
        }
        else {
            /* Update message credentials based on connected creds.
             */
            if (auth_init_message (msg, &conn->cred) < 0)
                goto error;

            if (conn->recv_cb)
                conn->recv_cb (conn, msg, conn->recv_arg);
            flux_msg_destroy (msg);
        }
    }
    return;
error:
    conn_io_error (conn, errno);
}

static int conn_outqueue_drop (struct usock_conn *conn)
{
    flux_msg_t *msg = zlist_pop (conn->outqueue);
    if (msg == NULL)
        return 0;
    flux_msg_decref (msg);
    return 1;
}

static void conn_write_cb (flux_reactor_t *r,
                           flux_watcher_t *w,
                           int revents,
                           void *arg)
{
    struct usock_conn *conn = arg;

    if ((revents & FLUX_POLLERR)) {
        errno = EIO;
        goto error;
    }

    if ((revents & FLUX_POLLOUT)) {
        const flux_msg_t *msg = zlist_head (conn->outqueue);
        if (msg) {
            if (sendfd (conn->out.fd, msg, &conn->out.iobuf) < 0) {
                if (errno == EPIPE) {
                    /* Remote peer has closed connection.
                     * However, there may still be pending messages sent
                     * by peer, so do not destroy connection here. Instead,
                     * drop all pending messages in the output queue, and
                     * let connection be closed after EOF/ECONNRESET from
                     * *read* side of connection.
                     */
                    while (conn_outqueue_drop (conn))
                        ;
                    flux_watcher_stop (conn->out.w);
                }
                else if (errno != EWOULDBLOCK && errno != EAGAIN)
                    goto error;
            }
            else {
                (void) conn_outqueue_drop (conn);
                if (zlist_size (conn->outqueue) == 0)
                    flux_watcher_stop (conn->out.w);
            }
        }
    }
    return;
error:
    conn_io_error (conn, errno);
}

static int write_char (int fd, unsigned char c)
{
    return write (fd, &c, 1);
}

/* Send 0 byte to client indicating auth success,
 * then put the fd in nonblocking mode and start the recv watcher.
 */
void usock_conn_accept (struct usock_conn *conn,
                        const struct flux_msg_cred *cred)
{
    if (conn && cred) {
        conn->cred = *cred;

        if (write_char (conn->out.fd, 0) < 0)
            goto error;
        if (fd_set_nonblocking (conn->in.fd) < 0)
            goto error;
        if (conn->in.fd != conn->out.fd) {
            if (fd_set_nonblocking (conn->out.fd) < 0)
                goto error;
        }

        flux_watcher_start (conn->in.w);
    }
    return;
error:
    conn_io_error (conn, errno);
}

/* Send nonzero byte (e.g. EPERM) to client indicating rejection.
 * It is left to the user to call usock_conn_destroy() when convenient.
 * N.B. the single byte allows the client to report more error detail than
 * would be possible if the connection were simply closed.
 */
void usock_conn_reject (struct usock_conn *conn, int errnum)
{
    if (conn)
        (void)write_char (conn->out.fd, errnum != 0 ? errnum : EPERM);
}

void usock_conn_destroy (struct usock_conn *conn)
{
    if (conn) {
        int saved_errno = errno;
        if (conn->close_cb)
            (*conn->close_cb) (conn, conn->close_arg);
        aux_destroy (&conn->aux);
        flux_watcher_destroy (conn->in.w);
        iobuf_clean (&conn->in.iobuf);
        if (conn->outqueue) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (conn->outqueue)))
                flux_msg_decref (msg);
            zlist_destroy (&conn->outqueue);
        }
        flux_watcher_destroy (conn->out.w);
        iobuf_clean (&conn->out.iobuf);
        if (conn->server)
            zlist_remove (conn->server->connections, conn);
        if (conn->enable_close_on_destroy) {
            if (conn->in.fd >= 0)
                (void)close (conn->in.fd);
            if (conn->out.fd != conn->in.fd && conn->out.fd >= 0)
                (void)close (conn->out.fd);
        }
        free (conn);
        errno = saved_errno;
    }
}

void usock_server_set_acceptor (struct usock_server *server,
                                usock_acceptor_f cb,
                                void *arg)
{
    if (server) {
        server->acceptor = cb;
        server->arg = arg;
    }
}

void usock_server_destroy (struct usock_server *server)
{
    if (server) {
        int saved_errno = errno;
        flux_watcher_destroy (server->w);
        if (server->fd >= 0) {
            (void)close (server->fd);
            (void)remove (server->sockpath);
        }
        if (server->connections) {
            struct usock_conn *conn;
            while ((conn = zlist_pop (server->connections))) {
                conn->server = NULL; // avoid redundant delist attempt
                usock_conn_destroy (conn);
            }
            zlist_destroy (&server->connections);
        }
        free (server->sockpath);
        free (server);
        errno = saved_errno;
    }
}

static int usock_get_cred (int fd, struct flux_msg_cred *cred)
{
    struct ucred ucred;
    socklen_t crlen;

    if (fd < 0 || !cred) {
        errno = EINVAL;
        return -1;
    }
    crlen = sizeof (ucred);
    if (getsockopt (fd,
                    SOL_SOCKET,
                    SO_PEERCRED,
                    &ucred,
                    &crlen) < 0)
        return -1;
    if (crlen != sizeof (ucred)) {
        errno = EPERM;
        return -1;
    }
    cred->userid = ucred.uid;
    cred->rolemask = FLUX_ROLE_NONE;
    return 0;
}

struct usock_conn *usock_conn_create (flux_reactor_t *r, int infd, int outfd)
{
    struct usock_conn *conn;

    if (!r || infd < 0 || outfd < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(conn = calloc (1, sizeof (*conn))))
        return NULL;

    conn->in.fd = infd;
    conn->out.fd = outfd;
    conn->cred.userid = FLUX_USERID_UNKNOWN;
    conn->cred.rolemask = FLUX_ROLE_NONE;

    if (!(conn->in.w = flux_fd_watcher_create (r,
                                               conn->in.fd,
                                               FLUX_POLLIN,
                                               conn_read_cb,
                                               conn)))
        goto error;
    iobuf_init (&conn->in.iobuf);

    if (!(conn->out.w = flux_fd_watcher_create (r,
                                                conn->out.fd,
                                                FLUX_POLLOUT,
                                                conn_write_cb,
                                                conn)))
        goto error;
    iobuf_init (&conn->out.iobuf);
    uuid_generate (conn->uuid);
    uuid_unparse (conn->uuid, conn->uuid_str);

    if (!(conn->outqueue = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    return conn;
error:
    usock_conn_destroy (conn);
    return NULL;
}

static struct usock_conn *server_accept (struct usock_server *server,
                                         flux_reactor_t *r)
{
    struct usock_conn *conn;
    int cfd;

#ifdef SOCK_CLOEXEC
    if ((cfd = accept4 (server->fd, NULL, NULL, SOCK_CLOEXEC)) < 0)
        return NULL;
#else
    if ((cfd = accept (server->fd, NULL, NULL)) < 0)
        return NULL;
    if (fd_set_cloexec (cfd) < 0) {
        ERRNO_SAFE_WRAP (close, cfd);
        return NULL;
    }
#endif
    if (!(conn = usock_conn_create (r, cfd, cfd))
        || usock_get_cred (cfd, &conn->cred) < 0) {
        ERRNO_SAFE_WRAP (close, cfd);
        return NULL;
    }
    conn->enable_close_on_destroy = 1;
    return conn;
}

static void timeout_cb (flux_reactor_t *r,
                        flux_watcher_t *w,
                        int revents,
                        void *arg)
{
    struct usock_server *server = arg;
    flux_watcher_start (server->w);
    flux_watcher_destroy (w);
}

static void server_cb (flux_reactor_t *r,
                       flux_watcher_t *w,
                       int revents,
                       void *arg)
{
    struct usock_server *server = arg;

    if ((revents & FLUX_POLLIN)) {
        struct usock_conn *conn;

        if (!(conn = server_accept (server, r))) {
            if (errno == ENFILE || errno == EMFILE) {
                /*  Too many open files. Do not just go back to sleep in the
                 *   reactor since we'll wake right back up again. Instead
                 *   disable this callback until after a short pause, giving
                 *   time for fds to be closed and have success on the next
                 *   try...
                 */
                flux_watcher_t *tw = flux_timer_watcher_create (r,
                                                                0.01,
                                                                0,
                                                                timeout_cb,
                                                                server);
                flux_watcher_start (tw);
                flux_watcher_stop (w);
            }
            return;
        }
        if (zlist_append (server->connections, conn) < 0) {
            usock_conn_destroy (conn);
            return;
        }
        conn->server = server; // now decref will also delist

        /* Acceptor should call (or arrange to later call) either
         * usock_conn_accept() or usock_conn_reject() to complete
         * auth handshake and start recv watcher.
         */
        if (server->acceptor)
            server->acceptor (conn, server->arg);
    }
}

struct usock_server *usock_server_create (flux_reactor_t *r,
                                          const char *sockpath,
                                          int mode)
{
    struct sockaddr_un addr;
    struct usock_server *server;

    if (!r || !sockpath) {
        errno = EINVAL;
        return NULL;
    }
    if (!(server = calloc (1, sizeof (*server))))
        return NULL;
#ifdef SOCK_CLOEXEC
    if ((server->fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
        goto error;
#else
    if ((server->fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0
        || fd_set_cloexec (server->fd) < 0)
        goto error;
#endif
    if (!(server->sockpath = strdup (sockpath)))
        goto error;
    if (remove (sockpath) < 0 && errno != ENOENT)
        goto error;

    memset (&addr, 0, sizeof (struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy (addr.sun_path, sockpath, sizeof (addr.sun_path) - 1);

    if (bind (server->fd, (struct sockaddr *)&addr,
              sizeof (struct sockaddr_un)) < 0)
        goto error;
    if (chmod (sockpath, mode) < 0)
        goto error;
    if (listen (server->fd, LISTEN_BACKLOG) < 0)
        goto error;
    if (!(server->w = flux_fd_watcher_create (r,
                                              server->fd,
                                              FLUX_POLLIN | FLUX_POLLERR,
                                              server_cb,
                                              server)))
        goto error;
    flux_watcher_start (server->w);
    if (!(server->connections = zlist_new ()))
        goto error;
    return server;
error:
    usock_server_destroy (server);
    return NULL;
}

static bool is_poll_error (int revents)
{
    if ((revents & POLLERR) || (revents & POLLHUP) || (revents & POLLNVAL))
        return true;
    return false;
}

/* Check which events are pending events on client fd (non-blocking).
 * If none are pending, return 0.  If an error occurred, return FLUX_POLLERR.
 * N.B. see op->pollevents in libflux/connector.h
 */
int usock_client_pollevents (struct usock_client *client)
{
    struct pollfd pfd;
    int flux_revents = 0;

    pfd.fd = client->fd;
    pfd.events = POLLIN | POLLOUT;
    pfd.revents = 0;

    if (poll (&pfd, 1, 0) < 0)
        return FLUX_POLLERR;
    if ((pfd.revents & POLLIN))
        flux_revents |= FLUX_POLLIN;
    if ((pfd.revents & POLLOUT))
        flux_revents |= FLUX_POLLOUT;
    if (is_poll_error (pfd.revents))
        flux_revents |= FLUX_POLLERR;

    return flux_revents;
}

/* Get a file descriptor that can be polled for events.
 * Upon wakeup, call usock_client_pollevents() to see what events occurred.
 * N.B. see op->pollfd in libflux/connector.h
 */
int usock_client_pollfd (struct usock_client *client)
{
    return client->fd;
}

/* Poll wrapper that blocks until the specified event occurs.
 * If an error occurs, return -1 with errno set.
 */
static int usock_client_poll (int fd, int events)
{
    struct pollfd pfd;

    memset (&pfd, 0, sizeof (pfd));
    pfd.fd = fd;
    pfd.events = events;

    if (poll (&pfd, 1, -1) < 0)
        return -1;
    if (is_poll_error (pfd.revents)) {
        errno = EIO;
        return -1;
    }
    return 0;
}

/* Try to send message.  If flags does not include FLUX_O_NONBLOCK,
 * and sendfd fails with EWOULDBLOCK/EAGAIN, then poll(POLLOUT) and
 * keep trying until the full message is sent.
 */
int usock_client_send (struct usock_client *client,
                       const flux_msg_t *msg,
                       int flags)
{
    while (sendfd (client->fd, msg, &client->out_iobuf) < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            return -1;
        if ((flags & FLUX_O_NONBLOCK))
            return -1;
        if (usock_client_poll (client->fd, POLLOUT) < 0)
            return -1;
    }
    return 0;
}

/* Try to recv message.  If flags does not include FLUX_O_NONBLOCK,
 * and recvfd fails with EWOULDBLOCK/EAGAIN, then poll(POLLIN) and
 * keep trying until the full message is received
 */
flux_msg_t *usock_client_recv (struct usock_client *client, int flags)
{
    flux_msg_t *msg;

    while (!(msg = recvfd (client->fd, &client->in_iobuf))) {
        if (errno != EWOULDBLOCK && errno != EAGAIN)
            return NULL;
        if ((flags & FLUX_O_NONBLOCK))
            return NULL;
        if (usock_client_poll (client->fd, POLLIN) < 0)
            return NULL;
    }
    return msg;
}

/* Open socket and connect it to 'sockpath'.
 * If connect fails, retry according to 'params'.
 */
int usock_client_connect (const char *sockpath,
                          struct usock_retry_params retry)
{
    int fd;
    struct sockaddr_un addr;
    useconds_t delay_usec = retry.min_delay * 1E6; // sec -> usec
    int retries = 0;

    if (!sockpath
        || strlen (sockpath) == 0
        || retry.max_retry < 0
        || retry.min_delay < 0
        || retry.max_delay < 0) {
        errno = EINVAL;
        return -1;
    }
#ifdef SOCK_CLOEXEC
    if ((fd = socket (AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
        return -1;
#else
    if ((fd = socket (AF_UNIX, SOCK_STREAM, 0)) < 0)
        return -1;
    if (fd_set_cloexec (fd) < 0)
        goto error;
#endif

    memset (&addr, 0, sizeof (addr));
    addr.sun_family = AF_UNIX;
    if (snprintf (addr.sun_path,
                  sizeof (addr.sun_path),
                  "%s",
                  sockpath) >= sizeof (addr.sun_path)) {
        errno = EINVAL;
        return -1;
    }
    while (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) < 0) {
        if (retries++ == retry.max_retry)
            goto error;
        usleep (delay_usec);
        delay_usec *= 2;
        if (delay_usec > retry.max_delay * 1E6)
            delay_usec = retry.max_delay * 1E6;
    }
    return fd;
error:
    ERRNO_SAFE_WRAP (close, fd);
    return -1;
}

/* Receive single-byte (0) response from server (auth handshake).
 * Return 0 on success, -1 on error with errno set.
 * If read returned a nonzero byte, use that as the errno value.
 */
static int usock_client_read_zero (int fd)
{
    unsigned char e;
    int n;

    if ((n = read (fd, &e, 1)) < 0)
        return -1;
    if (n == 0) {
        errno = ECONNRESET;
        return -1;
    }
    if (e != 0) {
        errno = e;
        return -1;
    }
    return 0;
}

struct usock_client *usock_client_create (int fd)
{
    struct usock_client *client;

    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(client = calloc (1, sizeof (*client))))
        return NULL;

    client->fd = fd;
    iobuf_init (&client->in_iobuf);
    iobuf_init (&client->out_iobuf);

    if (usock_client_read_zero (client->fd) < 0)
        goto error;
    if (fd_set_nonblocking (fd) < 0)
        goto error;
    return client;
error:
    usock_client_destroy (client);
    return NULL;
}

void usock_client_destroy (struct usock_client *client)
{
    if (client) {
        iobuf_clean (&client->in_iobuf);
        iobuf_clean (&client->out_iobuf);
        ERRNO_SAFE_WRAP (free, client);
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
