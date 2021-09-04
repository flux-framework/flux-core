/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* socketpair.c - shared memory channel from pmix server to shell plugin
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <zmq.h>

#include "src/common/libzmqutil/reactor.h"
#include "src/common/libzmqutil/msg_zsock.h"

#include "socketpair.h"

#define SOCKETPAIR_ENDPOINT   "inproc://pmix-socketpair"

struct socketpair {
    void *zctx;
    void *sock[2];
    socketpair_recv_f fun;
    void *arg;
    flux_watcher_t *w;
};

static void recv_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct socketpair *sp = arg;
    flux_msg_t *msg;

    if ((revents & FLUX_POLLIN)
        && (msg = zmqutil_msg_recv (sp->sock[1]))) {
        if (sp->fun)
            sp->fun (msg, sp->arg);
        flux_msg_decref (msg);
    }
}

int pp_socketpair_recv_register (struct socketpair *sp,
                                 socketpair_recv_f fun,
                                 void *arg)
{
    sp->fun = fun;
    sp->arg = arg;
    flux_watcher_start (sp->w);
    return 0;
}

int pp_socketpair_send_pack (struct socketpair *sp,
                             const char *name,
                             const char *fmt, ...)
{
    flux_msg_t *msg = NULL;
    va_list ap;
    int rc = 0;

    va_start (ap, fmt);
    if (!(msg = flux_msg_create (FLUX_MSGTYPE_EVENT))
        || flux_msg_set_topic (msg, name) < 0
        || flux_msg_vpack (msg, fmt, ap) < 0
        || zmqutil_msg_send (sp->sock[0], msg) < 0) {
        rc = -1;
    }
    va_end (ap);
    flux_msg_decref (msg);

    return rc;
}

void pp_socketpair_destroy (struct socketpair *sp)
{
    if (sp) {
        int saved_errno = errno;
        flux_watcher_destroy (sp->w);

        zmq_disconnect (sp->sock[0], SOCKETPAIR_ENDPOINT);
        zmq_close (sp->sock[0]);

        zmq_unbind (sp->sock[1], SOCKETPAIR_ENDPOINT);
        zmq_close (sp->sock[1]);

        zmq_ctx_term (sp->zctx);
        errno = saved_errno;
    }
}

struct socketpair *pp_socketpair_create (flux_reactor_t *r)
{
    struct socketpair *sp;

    if (!(sp = calloc (1, sizeof (*sp))))
        return NULL;
    if (!(sp->zctx = zmq_ctx_new ()))
        goto error;

    if (!(sp->sock[1] = zmq_socket (sp->zctx, ZMQ_PULL)))
        goto error;
    if (zmq_bind (sp->sock[1], SOCKETPAIR_ENDPOINT) < 0)
        goto error;
    if (!(sp->w = zmqutil_watcher_create (r,
                                          sp->sock[1],
                                          FLUX_POLLIN,
                                          recv_cb,
                                          sp)))
        goto error;

    if (!(sp->sock[0] = zmq_socket (sp->zctx, ZMQ_PUSH)))
        goto error;
    if (zmq_connect (sp->sock[0], SOCKETPAIR_ENDPOINT) < 0)
        goto error;

    return sp;
error:
    pp_socketpair_destroy (sp);
    return NULL;
}

// vi:tabstop=4 shiftwidth=4 expandtab
