/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _PMIX_SOCKETPAIR_H
#define _PMIX_SOCKETPAIR_H

#include <stdarg.h>
#include <flux/core.h>
#include <czmq.h>
#include <zmq.h>

/* Create and destroy socketpair from the shell thread.
 */
struct socketpair *pp_socketpair_create (flux_reactor_t *r);
void pp_socketpair_destroy (struct socketpair *sp);

/* Register receive callback that will be called in the shell thread
 * for each message sent by the server.
 */
typedef void (*socketpair_recv_f)(const flux_msg_t *msg, void *arg);

int pp_socketpair_recv_register (struct socketpair *sp,
                                 socketpair_recv_f fun,
                                 void *arg);

/* Send function must be called from the server thread ONLY.
 */
int pp_socketpair_send_pack (struct socketpair *sp,
                             const char *name,
                             const char *fmt, ...);


#endif

// vi:tabstop=4 shiftwidth=4 expandtab
