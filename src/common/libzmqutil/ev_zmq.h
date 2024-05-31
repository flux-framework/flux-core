/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _EV_ZMQ_H
#define _EV_ZMQ_H

#include <stdbool.h>
#include "src/common/libev/ev.h"

typedef struct ev_zmq_struct ev_zmq;
typedef void (*ev_zmq_cb)(struct ev_loop *loop, ev_zmq *w, int revents);

struct ev_zmq_struct {
    ev_io       io_w;
    ev_prepare  prepare_w;
    ev_idle     idle_w;
    ev_check    check_w;
    void        *zsock;
    int         fd;
    int         events;
    ev_zmq_cb   cb;
    void        *data;
};

int ev_zmq_init (ev_zmq *w, ev_zmq_cb cb, void *zsock, int events);
void ev_zmq_start (struct ev_loop *loop, ev_zmq *w);
void ev_zmq_stop (struct ev_loop *loop, ev_zmq *w);
bool ev_zmq_is_active (ev_zmq *w);

/* Convert zeromq poll bits to libev's, for construction of 'events'
 * when registering a watcher.
 */
static __inline__ int ztoe (int z)
{
    int e = 0;
    if ((z & ZMQ_POLLIN))
        e |= EV_READ;
    if ((z & ZMQ_POLLOUT))
        e |= EV_WRITE;
#if 0
    /* Note: libev will assert if EV_ERROR is included in 'events'.
     * If there is an error, libev will call your callback with EV_ERROR set
     * whether you request it or not.  Silently ignore ZMQ_POLLERR here.
     */
    if ((z & ZMQ_POLLERR))
        e |= EV_ERROR;
#endif
    return e;
}

/* Convert libev poll bits to zeromq's, for interpreting 'revents' from
 * a libev callback in zeromq context.
 */
static __inline__ int etoz (int e)
{
    int z = 0;
    if ((e & EV_READ))
        z |= ZMQ_POLLIN;
    if ((e & EV_WRITE))
        z |= ZMQ_POLLOUT;
    if ((e & EV_ERROR))
        z |= ZMQ_POLLERR;
    return z;
}

#endif /* !_EV_ZMQ_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

