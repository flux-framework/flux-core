/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* sendfd.c - send and receive flux_msg_t over file descriptors
 *
 * These functions use the following encoding for each message:
 *
 *   4 bytes - IOBUF_MAGIC
 *   4 bytes - size in network byte order, includes magic and size
 *   N bytes - message encoded with flux_msg_encode()
 *
 * These functions work with file descriptors configured for either
 * blocking or non-blocking modes.  In blocking mode, the iobuf
 * argument may be set to NULL.  In non-blocking mode, an iobuf should
 * be provided to allow messages to be assembled across multiple calls.
 *
 * In non-blocking mode, sendfd() or recfd() may fail with EWOULDBLOCK
 * or EAGAIN.  This should not be treated as an error.  When poll(2) or
 * equivalent indicates that the file descriptor is ready again, sendfd()
 * or recvfd() may be called again, continuing I/O to/from the same iobuf.
 *
 * Separate iobufs are required for sendfd() and recvfd().
 * Call iobuf_init() on an iobuf before its first use.
 * Call iobuf_clean() on an iobuf after its last use.
 * The iobuf is managed by sendfd() and recvfd() across multiple messages.
 *
 * Notes:
 *
 * - to decrease small message latency, the iobuf contains a fixed size
 *   static buffer.  When a message requires more than this fixed size for
 *   assembly, a dynamic buffer is allocated temporarily while that message
 *   is assembled, then it is freed.  The static buffer is sized somewhat
 *   arbitrarily at 4K.
 *
 * - sendfd/recvfd do not encrypt messages, therefore this transport
 *   is only appropriate for use on AF_LOCAL sockets or on file descriptors
 *   tunneled through a secure channel.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <arpa/inet.h>
#include <unistd.h>
#include <flux/core.h>

#include "sendfd.h"

#define IOBUF_MAGIC 0xffee0012

void iobuf_init (struct iobuf *iobuf)
{
    memset (iobuf, 0, sizeof (*iobuf));
}

void iobuf_clean (struct iobuf *iobuf)
{
    if (iobuf->buf && iobuf->buf != iobuf->buf_fixed)
        free (iobuf->buf);
    memset (iobuf, 0, sizeof (*iobuf));
}

int sendfd (int fd, const flux_msg_t *msg, struct iobuf *iobuf)
{
    struct iobuf local;
    struct iobuf *io = iobuf ? iobuf : &local;
    int rc = -1;

    if (fd < 0 || !msg) {
        errno = EINVAL;
        return -1;
    }
    if (!iobuf)
        iobuf_init (&local);
    if (!io->buf) {
        io->size = flux_msg_encode_size (msg) + 8;
        if (io->size <= sizeof (io->buf_fixed))
            io->buf = io->buf_fixed;
        else if (!(io->buf = malloc (io->size)))
            goto done;
        *(uint32_t *)&io->buf[0] = IOBUF_MAGIC;
        *(uint32_t *)&io->buf[4] = htonl (io->size - 8);
        if (flux_msg_encode (msg, &io->buf[8], io->size - 8) < 0)
            goto done;
        io->done = 0;
    }
    do {
        rc = write (fd, io->buf + io->done, io->size - io->done);
        if (rc < 0)
            goto done;
        io->done += rc;
    } while (io->done < io->size);
    rc = 0;
done:
    if (iobuf) {
        if (rc == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
            iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        iobuf_clean (&local);
    }
    return rc;
}

flux_msg_t *recvfd (int fd, struct iobuf *iobuf)
{
    struct iobuf local;
    struct iobuf *io = iobuf ? iobuf : &local;
    flux_msg_t *msg = NULL;
    int rc = -1;

    if (fd < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!iobuf)
        iobuf_init (&local);
    if (!io->buf) {
        io->buf = io->buf_fixed;
        io->size = sizeof (io->buf_fixed);
    }
    do {
        if (io->done < 8) {
            rc = read (fd, io->buf + io->done, 8 - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = ECONNRESET;
                goto done;
            }
            io->done += rc;
            if (io->done == 8) {
                if (*(uint32_t *)&io->buf[0] != IOBUF_MAGIC) {
                    errno = EPROTO;
                    goto done;
                }
                io->size = ntohl (*(uint32_t *)&io->buf[4]) + 8;
                if (io->size > sizeof (io->buf_fixed)) {
                    if (!(io->buf = malloc (io->size)))
                        goto done;
                    memcpy (io->buf, io->buf_fixed, 8);
                }
            }
        }
        if (io->done >= 8 && io->done < io->size) {
            rc = read (fd, io->buf + io->done, io->size - io->done);
            if (rc < 0)
                goto done;
            if (rc == 0) {
                errno = ECONNRESET;
                goto done;
            }
            io->done += rc;
        }
    } while (io->done < io->size);
    if (!(msg = flux_msg_decode (io->buf + 8, io->size - 8)))
        goto done;
done:
    if (iobuf) {
        if (msg != NULL || (errno != EAGAIN && errno != EWOULDBLOCK))
            iobuf_clean (iobuf);
    } else {
        if (rc < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            errno = EPROTO;
        iobuf_clean (&local);
    }
    return msg;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

