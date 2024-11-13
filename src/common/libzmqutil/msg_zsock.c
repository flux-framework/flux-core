/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
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
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <zmq.h>
#include <flux/core.h>

#include "src/common/libflux/message_iovec.h"
#include "src/common/libflux/message_proto.h"
#include "src/common/libutil/errno_safe.h"

#include "sockopt.h"
#include "msg_zsock.h"

int zmqutil_msg_send_ex (void *sock, const flux_msg_t *msg, bool nonblock)
{
    int flags = ZMQ_SNDMORE;
    struct msg_iovec *iov = NULL;
    int iovcnt;
    uint8_t proto[PROTO_SIZE];
    int count = 0;
    int rc = -1;

    if (!sock || !msg) {
        errno = EINVAL;
        return -1;
    }

    if (msg_to_iovec (msg, proto, PROTO_SIZE, &iov, &iovcnt) < 0)
        goto error;

    if (nonblock)
        flags |= ZMQ_DONTWAIT;

    while (count < iovcnt) {
        if ((count + 1) == iovcnt)
            flags &= ~ZMQ_SNDMORE;
        if (zmq_send (sock,
                      iov[count].data,
                      iov[count].size,
                      flags) < 0)
            goto error;
        count++;
    }
    rc = 0;
error:
    ERRNO_SAFE_WRAP (free, iov);
    return rc;
}

int zmqutil_msg_send (void *sock, const flux_msg_t *msg)
{
    return zmqutil_msg_send_ex (sock, msg, false);
}

flux_msg_t *zmqutil_msg_recv (void *sock)
{
    struct msg_iovec *iov = NULL;
    int iovlen = 0;
    int iovcnt = 0;
    flux_msg_t *msg;
    flux_msg_t *rv = NULL;

    if (!sock) {
        errno = EINVAL;
        return NULL;
    }

    /* N.B. we need to store a zmq_msg_t for each iovec entry so that
     * the memory is available during the call to iovec_to_msg().  We
     * use the msg_iovec's "transport_data" field to store the entry
     * and then clear/free it later.
     */
    while (true) {
        zmq_msg_t *msgdata;
        if (iovlen <= iovcnt) {
            struct msg_iovec *tmp;
            iovlen += IOVECINCR;
            if (!(tmp = realloc (iov, sizeof (*iov) * iovlen)))
                goto error;
            iov = tmp;
        }
        if (!(msgdata = malloc (sizeof (zmq_msg_t))))
            goto error;
        zmq_msg_init (msgdata);
        if (zmq_recvmsg (sock, msgdata, 0) < 0) {
            int save_errno = errno;
            zmq_msg_close (msgdata);
            free (msgdata);
            errno = save_errno;
            goto error;
        }
        iov[iovcnt].transport_data = msgdata;
        iov[iovcnt].data = zmq_msg_data (msgdata);
        iov[iovcnt].size = zmq_msg_size (msgdata);
        iovcnt++;

        int rcvmore;
        if (zgetsockopt_int (sock, ZMQ_RCVMORE, &rcvmore) < 0)
            goto error;
        if (!rcvmore)
            break;
    }

    if (!(msg = iovec_to_msg (iov, iovcnt)))
        goto error;
    rv = msg;
error:
    if (iov) {
        int save_errno = errno;
        int i;
        for (i = 0; i < iovcnt; i++) {
            zmq_msg_t *msgdata = iov[i].transport_data;
            zmq_msg_close (msgdata);
            free (msgdata);
        }
        free (iov);
        errno = save_errno;
    }
    return rv;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

