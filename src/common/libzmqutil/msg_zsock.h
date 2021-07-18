/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ZMQUTIL_MSG_ZSOCK_H
#define _ZMQUTIL_MSG_ZSOCK_H

#include <stdio.h>
#include <stdlib.h>

#include "src/common/libflux/message.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Send message to zeromq socket.
 * Returns 0 on success, -1 on failure with errno set.
 */
int zmqutil_msg_send (void *dest, const flux_msg_t *msg);
int zmqutil_msg_send_ex (void *dest, const flux_msg_t *msg, bool nonblock);

/* Receive a message from zeromq socket.
 * Returns message on success, NULL on failure with errno set.
 */
flux_msg_t *zmqutil_msg_recv (void *dest);

#ifdef __cplusplus
}
#endif

#endif /* !_ZMQUTIL_MSG_ZSOCK_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

