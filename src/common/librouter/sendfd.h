/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_SENDFD_H
#define _ROUTER_SENDFD_H

#include <flux/core.h>

struct iobuf {
    uint8_t *buf;
    size_t size;
    size_t done;
    uint8_t buf_fixed[4096];
};

/* Send message to file descriptor.
 * iobuf captures intermediate state to make EAGAIN/EWOULDBLOCK restartable.
 * Returns 0 on success, -1 on failure with errno set.
 */
int sendfd (int fd, const flux_msg_t *msg, struct iobuf *iobuf);

/* Receive message from file descriptor.
 * iobuf captures intermediate state to make EAGAIN/EWOULDBLOCK restartable.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *recvfd (int fd, struct iobuf *iobuf);

/* Initialize iobuf members.
 */
void iobuf_init (struct iobuf *iobuf);

/* Free any internal memory allocated to iobuf.
 * Only necessary if destroying with partial I/O in progress.
 */
void iobuf_clean (struct iobuf *iobuf);

#endif /* !_ROUTER_SENDFD_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

