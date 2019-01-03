/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* ev_zmq.c - an aggregate libev watcher for 0MQ sockets */

/* Thanks to Bert JW Regeer for a helpful blog on integrating 0MQ with libev:
 *   http://funcptr.net/2013/04/20/embedding-zeromq-in-the-libev-event-loop/
 * Also ref: libzmq zmq_poll() source, czmq zloop() source
 *
 * Brief summary of 0MQ integration:
 * - 0MQ provides ZMQ_EVENTS getsockopt to test whether a 0MQ socket is
 *   writeable or readable.
 * - 0MQ provides ZMQ_FD getsockopt to obtain the fd of a mailbox that
 *   becomes readable when ZMQ_EVENTS != 0 (edge triggered)
 * - libev prepare/check callbacks are used to test ZMQ_EVENTS, make user
 *   callbacks, and enable/disable no-op io and idle watchers.
 * - while ZMQ_EVENTS != 0, enable no-op idle watcher (no callback)
 *   so that the libev loop will continue looping, executing prepare/check
 * - when ZMQ_EVENTS == 0, enable no-op io watcher on ZMQ_FD (no callback)
 *   so that the libev loop will unblock, executing prepare/check
 *   on the next mailbox event
 */

#include <czmq.h>
#include <zmq.h>
#include <stdbool.h>

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"

static void prepare_cb (struct ev_loop *loop, ev_prepare *w, int revents)
{
    ev_zmq *zw = (ev_zmq *)((char *)w - offsetof (ev_zmq, prepare_w));
    uint32_t zevents = 0;
    size_t zevents_size = sizeof (zevents);
    void *handle = zsock_resolve (zw->zsock);

    if (handle == NULL)
        ev_idle_start (loop, &zw->idle_w);
    else if (zmq_getsockopt (handle, ZMQ_EVENTS, &zevents, &zevents_size) < 0)
        ev_idle_start (loop, &zw->idle_w);
    else if ((revents = ztoe (zevents) & zw->events))
        ev_idle_start (loop, &zw->idle_w);
    else
        ev_io_start (loop, &zw->io_w);
}

static void check_cb (struct ev_loop *loop, ev_check *w, int revents)
{
    ev_zmq *zw = (ev_zmq *)((char *)w - offsetof (ev_zmq, check_w));
    uint32_t zevents = 0;
    size_t zevents_size = sizeof (zevents);
    void *handle = zsock_resolve (zw->zsock);

    ev_io_stop (loop, &zw->io_w);
    ev_idle_stop (loop, &zw->idle_w);

    if (handle == NULL)
        zw->cb (loop, zw, EV_ERROR);
    else if (zmq_getsockopt (handle, ZMQ_EVENTS, &zevents, &zevents_size) < 0)
        zw->cb (loop, zw, EV_ERROR);
    else if ((revents = ztoe (zevents) & zw->events))
        zw->cb (loop, zw, revents);
}

int ev_zmq_init (ev_zmq *w, ev_zmq_cb cb, void *zsock, int events)
{
    w->cb = cb;
    w->zsock = zsock;
    w->events = events;
    size_t fd_size = sizeof (w->fd);
    void *handle = zsock_resolve (zsock);

    if (handle == NULL)
        return -1;
    if (zmq_getsockopt (handle, ZMQ_FD, &w->fd, &fd_size) < 0)
        return -1;

    ev_prepare_init (&w->prepare_w, prepare_cb);
    ev_check_init (&w->check_w, check_cb);
    ev_idle_init (&w->idle_w, NULL);
    ev_io_init (&w->io_w, NULL, w->fd, EV_READ);

    return 0;
}

void ev_zmq_start (struct ev_loop *loop, ev_zmq *w)
{
    ev_prepare_start (loop, &w->prepare_w);
    ev_check_start (loop, &w->check_w);
}

void ev_zmq_stop (struct ev_loop *loop, ev_zmq *w)
{
    ev_prepare_stop (loop, &w->prepare_w);
    ev_check_stop (loop, &w->check_w);
    ev_io_stop (loop, &w->io_w);
    ev_idle_stop (loop, &w->idle_w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

