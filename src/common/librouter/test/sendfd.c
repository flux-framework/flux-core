/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifndef HAVE_PIPE2
#include "src/common/libmissing/pipe2.h"
#endif


#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/librouter/sendfd.h"
#include "src/common/libutil/fdutils.h"
#include "src/common/libtap/tap.h"
#include "ccan/str/str.h"

/* Send a small message over a blocking pipe.
 * We assume that there's enough buffer to do this in one go.
 */
void test_basic (void)
{
    int pfd[2];
    flux_msg_t *msg, *msg2;
    const char *topic;
    const char *payload = NULL;

    if (pipe2 (pfd, O_CLOEXEC) < 0)
        BAIL_OUT ("pipe2 failed");
    if (!(msg = flux_request_encode ("foo.bar", NULL)))
        BAIL_OUT ("flux_request_encode failed");
    ok (sendfd (pfd[1], msg, NULL) == 0,
        "sendfd works");
    ok ((msg2 = recvfd (pfd[0], NULL)) != NULL,
        "recvfd works");
    ok (flux_request_decode (msg2, &topic, &payload) == 0,
        "received request can be decoded");
    ok (streq (topic, "foo.bar"),
        "decoded request has expected topic string");
    ok (payload == NULL,
        "decoded request has expected (lack of) payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);
    close (pfd[1]);
    close (pfd[0]);
}

/* Send a large (>4k static buffer) message over a blocking pipe.
 */
void test_large (void)
{
    int pfd[2];
    flux_msg_t *msg, *msg2;
    char buf[8192];
    const char *topic;
    const void *buf2;
    size_t buf2len;
#if defined(F_GETPIPE_SZ)
    int min_size = 16384;
    int size;
#endif

    memset (buf, 0x0f, sizeof (buf));

    if (pipe2 (pfd, O_CLOEXEC) < 0)
        BAIL_OUT ("pipe2 failed");

#if defined(F_GETPIPE_SZ)
    size = fcntl (pfd[1], F_GETPIPE_SZ);
    if (size < min_size)
        (void)fcntl (pfd[1], F_SETPIPE_SZ, min_size);
    size = fcntl (pfd[1], F_GETPIPE_SZ);
    skip (size < min_size, 4, "%d byte pipe is too small", size);
#else
    skip (true, 4, "F_GETPIPE_SZ not defined");
#endif
    if (!(msg = flux_request_encode_raw ("foo.bar", buf, sizeof (buf))))
        BAIL_OUT ("flux_request_encode failed");
    ok (sendfd (pfd[1], msg, NULL) == 0,
        "sendfd works");
    ok ((msg2 = recvfd (pfd[0], NULL)) != NULL,
        "recvfd works");
    ok (flux_request_decode_raw (msg2, &topic, &buf2, &buf2len) == 0,
        "received request can be decoded");
    ok (streq (topic, "foo.bar"),
        "decoded request has expected topic string");
    ok (buf2 != NULL
        && buf2len == sizeof (buf)
        && memcmp (buf, buf2, buf2len) == 0,
        "decoded request has expected payload");

    flux_msg_destroy (msg);
    flux_msg_destroy (msg2);

    end_skip;

    close (pfd[1]);
    close (pfd[0]);
}

/* Close the sending end of a blocking pipe and ensure the
 * receiving end gets ECONNRESET.
 */
void test_eof (void)
{
    int pfd[2];

    if (pipe2 (pfd, O_CLOEXEC) < 0)
        BAIL_OUT ("pipe2 failed");
    close (pfd[1]);
    errno = 0;
    ok (recvfd (pfd[0], NULL) == NULL && errno == ECONNRESET,
        "recvfd fails with ECONNRESET when sender closes pipe");
    close (pfd[0]);
}

struct io {
    zlist_t *queue;
    struct iobuf iobuf;
    int fd;
    flux_watcher_t *w;
    int max;
};

void recv_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct io *io = arg;

    if ((revents & FLUX_POLLERR))
        BAIL_OUT ("recv_cb POLLERR");
    if ((revents & FLUX_POLLIN)) {
        flux_msg_t *msg;
        if (!(msg = recvfd (io->fd, &io->iobuf))) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                diag ("recv EWOULDBLOCK");
                return;
            }
            BAIL_OUT ("recvfd error: %s", strerror (errno));
        }
        if (zlist_append (io->queue, msg) < 0)
            BAIL_OUT ("zlist_append failed");
        if (zlist_size (io->queue) == io->max) {
            diag ("recv queue full, stopping receiver");
            flux_watcher_stop (io->w);
        }
    }
}

void send_cb (flux_reactor_t *r, flux_watcher_t *w, int revents, void *arg)
{
    struct io *io = arg;

    if ((revents & FLUX_POLLERR))
        BAIL_OUT ("recv_cb POLLERR");
    if ((revents & FLUX_POLLOUT)) {
        flux_msg_t *msg;

        if ((msg = zlist_first (io->queue))) {
            if (sendfd (io->fd, msg, &io->iobuf) < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    diag ("send EWOULDBLOCK");
                    return;
                }
                BAIL_OUT ("sendfd error: %s", strerror (errno));
            }
            (void)zlist_pop (io->queue);
            flux_msg_destroy (msg);
        }
        else {
            diag ("send queue empty, stopping sender");
            flux_watcher_stop (io->w);
        }
    }
}

void io_destroy (struct io *io)
{
    if (io) {
        int saved_errno = errno;
        if (io->queue) {
            flux_msg_t *msg;
            while ((msg = zlist_pop (io->queue)))
                flux_msg_destroy (msg);
            zlist_destroy (&io->queue);
        }
        flux_watcher_destroy (io->w);
        iobuf_clean (&io->iobuf);
        free (io);
        errno = saved_errno;
    }
}

struct io *io_create (flux_reactor_t *r,
                      int fd,
                      int flags,
                      flux_watcher_f cb)
{
    struct io *io;
    if (!(io = calloc (1, sizeof (*io))))
        return NULL;
    iobuf_init (&io->iobuf);
    if (!(io->queue = zlist_new ()))
        goto error;
    io->fd = fd;
    if (fd_set_nonblocking (fd) < 0)
        goto error;
    if (!(io->w = flux_fd_watcher_create (r, fd, flags, cb, io)))
        goto error;
    flux_watcher_start (io->w);
    return io;
error:
    io_destroy (io);
    return NULL;
}

/* Enqueue 'count' messages with payload 'size'.
 * Set up nonblocking sender and receiver.
 * Run the reactor:
 * - sender sends all enqueued messages
 * - receiver enqueues all received messages
 * Verify that messages are all received intact.
 */
void test_nonblock (int size, int count)
{
    int pfd[2];
    struct io *iow;
    struct io *ior;
    flux_reactor_t *r;
    int i;
    char *buf;
    int errors;
    flux_msg_t *msg;

    if (!(buf = malloc (size)))
        BAIL_OUT ("malloc failed");
    memset (buf, 0xf0, size);

    if (!(r = flux_reactor_create (0)))
        BAIL_OUT ("flux_reactor_create failed");
    if (pipe2 (pfd, O_CLOEXEC) < 0)
        BAIL_OUT ("pipe2 failed");
    if (!(iow = io_create (r, pfd[1], FLUX_POLLOUT, send_cb)))
        BAIL_OUT ("io_create failed: %s", flux_strerror (errno));
    if (!(ior = io_create (r, pfd[0], FLUX_POLLIN, recv_cb)))
        BAIL_OUT ("io_create failed: %s", flux_strerror (errno));

    for (i = 0; i < count; i++) {
        if (!(msg = flux_request_encode_raw ("foo.bar", buf, size)))
            BAIL_OUT ("flux_request_encode failed");
        if (zlist_append (iow->queue, msg) < 0)
            BAIL_OUT ("zlist_append failed");
    }
    ior->max = count;

    diag ("messages enqueued, starting reactor", count);

    ok (flux_reactor_run (r, 0) == 0,
        "nonblock %d,%d: reactor ran", count, size);

    ok (zlist_size (ior->queue) == count,
        "nonblock %d,%d: all messages received",
        count,
        size);

    errors = 0;
    while ((msg = zlist_pop (ior->queue))) {
        const char *topic;
        const void *buf2;
        size_t buf2len;

        if (flux_request_decode_raw (msg, &topic, &buf2, &buf2len) < 0) {
            diag ("flux_request_decode_raw: %s", flux_strerror (errno));
            errors++;
            goto next;
        }
        if (!streq (topic, "foo.bar")) {
            diag ("decoded wrong topic: %s", topic);
            errors++;
            goto next;
        }
        if (buf2len != size || memcmp (buf, buf2, buf2len) != 0) {
            diag ("decoded payload incorrectly");
            errors++;
            goto next;
        }
next:
        flux_msg_destroy (msg);
    }

    ok (errors == 0,
        "nonblock %d,%d: received messages are intact",
        count,
        size);

    io_destroy (iow);
    io_destroy (ior);
    close (pfd[1]);
    close (pfd[0]);
    flux_reactor_destroy (r);
    free (buf);
}

void test_inval (void)
{
    flux_msg_t *msg;

    if (!(msg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        BAIL_OUT ("flux_msg_create failed");

    errno = 0;
    ok (recvfd (-1, NULL) == NULL && errno == EINVAL,
        "recvfd fd=-1 fails with EINVAL");

    errno = 0;
    ok (sendfd (-1, msg, NULL) < 0 && errno == EINVAL,
        "sendfd fd=-1 fails with EINVAL");
    errno = 0;
    ok (sendfd (0, NULL, NULL) < 0 && errno == EINVAL,
        "senfd msg=NULL fails with EINVAL");

    flux_msg_destroy (msg);
}

int main (int argc, char *argv[])
{
    plan (NO_PLAN);

    test_basic ();
    test_large ();
    test_eof ();
    test_nonblock (1024, 1024);
    test_nonblock (4096, 256);
    test_nonblock (16384, 64);
    test_nonblock (1048586, 1);
    test_inval ();

    done_testing();
    return (0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

