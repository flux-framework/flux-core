/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_MESSAGE_IOVEC_H
#define _FLUX_CORE_MESSAGE_IOVEC_H

#define IOVECINCR           4

/* 'transport_data' is for any auxiliary transport data user may wish
 * to associate with iovec, user is responsible to free/destroy the
 * field
 */
struct msg_iovec {
    const void *data;
    size_t size;
    void *transport_data;
};

flux_msg_t *iovec_to_msg (struct msg_iovec *iov, int iovcnt);

int msg_to_iovec (const flux_msg_t *msg,
                  uint8_t *proto,
                  int proto_len,
                  struct msg_iovec **iovp,
                  int *iovcntp);

#endif /* !_FLUX_CORE_MESSAGE_IOVEC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

