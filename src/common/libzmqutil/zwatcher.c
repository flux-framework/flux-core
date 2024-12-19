/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* zwatcher.c - an aggregate libev watcher for 0MQ sockets */

/* Thanks to Bert JW Regeer for a helpful blog on integrating 0MQ with libev:
 *   http://funcptr.net/2013/04/20/embedding-zeromq-in-the-libev-event-loop/
 * Also ref: libzmq zmq_poll() source, czmq zloop() source
 *
 * Brief summary of 0MQ integration:
 * - 0MQ provides ZMQ_EVENTS getsockopt to test whether a 0MQ socket is
 *   writeable or readable.
 * - 0MQ provides ZMQ_FD getsockopt to obtain the fd of a mailbox that
 *   becomes readable when ZMQ_EVENTS != 0 (edge triggered)
 * - prepare/check watchers are used to test ZMQ_EVENTS, make user
 *   callbacks, and enable/disable no-op io and idle watchers.
 * - while ZMQ_EVENTS != 0, enable no-op idle watcher (no callback)
 *   so that the event loop will continue looping, executing prepare/check
 * - when ZMQ_EVENTS == 0, enable no-op io watcher on ZMQ_FD (no callback)
 *   so that the event loop will unblock, executing prepare/check
 *   on the next mailbox event
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <zmq.h>
#include <flux/core.h>

#include "src/common/libflux/watcher_private.h"
#include "src/common/libutil/errno_safe.h"

#include "zwatcher.h"

struct zwatcher {
    flux_watcher_t *fd_w;
    flux_watcher_t *prepare_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    void *zsock;
    int events;
};

static int get_zmq_fd (void *zsock, int *fd)
{
    int val;
    size_t size = sizeof (val);

    if (!zsock) {
        errno = EINVAL;
        return -1;
    }
    if (zmq_getsockopt (zsock, ZMQ_FD, &val, &size) < 0)
        return -1;
    *fd = val;
    return 0;
}

static int ztof (int zevents)
{
    int fevents = 0;
    if ((zevents & ZMQ_POLLIN))
        fevents |= FLUX_POLLIN;
    if ((zevents & ZMQ_POLLOUT))
        fevents |= FLUX_POLLOUT;
    return fevents;
}

static int get_zmq_events (void *zsock, int *events)
{
    int val;
    size_t size = sizeof (val);

    if (!zsock) {
        errno = EINVAL;
        return -1;
    }
    if (zmq_getsockopt (zsock, ZMQ_EVENTS, &val, &size) < 0)
        return -1;
    *events = ztof (val);
    return 0;
}

static void zwatcher_start (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);

    flux_watcher_start (zw->prepare_w);
    flux_watcher_start (zw->check_w);
}

static void zwatcher_stop (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);

    flux_watcher_stop (zw->prepare_w);
    flux_watcher_stop (zw->check_w);
    flux_watcher_stop (zw->fd_w);
    flux_watcher_stop (zw->idle_w);
}

static void zwatcher_ref (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);

    flux_watcher_ref (zw->fd_w);
    flux_watcher_ref (zw->prepare_w);
    flux_watcher_ref (zw->idle_w);
    flux_watcher_ref (zw->check_w);
}

static void zwatcher_unref (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);

    flux_watcher_unref (zw->fd_w);
    flux_watcher_unref (zw->prepare_w);
    flux_watcher_unref (zw->idle_w);
    flux_watcher_unref (zw->check_w);
}

static bool zwatcher_is_active (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);

    return flux_watcher_is_active (zw->prepare_w);
}

static void zwatcher_destroy (flux_watcher_t *w)
{
    struct zwatcher *zw = watcher_get_data (w);
    if (zw) {
        flux_watcher_destroy (zw->prepare_w);
        flux_watcher_destroy (zw->check_w);
        flux_watcher_destroy (zw->fd_w);
        flux_watcher_destroy (zw->idle_w);
    }
}

static void prepare_cb (flux_reactor_t *r,
                        flux_watcher_t *prepare_w,
                        int ignore,
                        void *arg)
{
    flux_watcher_t *w = arg;
    struct zwatcher *zw = watcher_get_data (w);
    int zevents;

    if (get_zmq_events (zw->zsock, &zevents) < 0)
        zevents = FLUX_POLLERR;

    if ((zevents & zw->events))
        flux_watcher_start (zw->idle_w);
    else
        flux_watcher_start (zw->fd_w);
}

static void check_cb (flux_reactor_t *r,
                      flux_watcher_t *check_w,
                      int ignore,
                      void *arg)
{
    flux_watcher_t *w = arg;
    struct zwatcher *zw = watcher_get_data (w);
    int zevents;
    int revents;

    flux_watcher_stop (zw->fd_w);
    flux_watcher_stop (zw->idle_w);

    if (get_zmq_events (zw->zsock, &zevents) < 0)
        zevents = FLUX_POLLERR;
    revents = (zevents & zw->events);

    if (revents)
        watcher_call (w, revents);
}

/* N.B. The internal fd watcher is only used for its side effect of
 * unblocking the reactor when pollevents edge triggers from "no events"
 * to "some events".  The prep/check watchers do the heavy lifting.
 * This callback exists only to handle POLLERR in case something goes wrong.
 */
static void fd_cb (flux_reactor_t *r,
                   flux_watcher_t *fd_w,
                   int revents,
                   void *arg)
{
    flux_watcher_t *w = arg;

    if ((revents & FLUX_POLLERR))
        watcher_call (w, FLUX_POLLERR);
}

static struct flux_watcher_ops zwatcher_ops = {
    .start = zwatcher_start,
    .stop = zwatcher_stop,
    .ref = zwatcher_ref,
    .unref = zwatcher_unref,
    .destroy = zwatcher_destroy,
    .is_active = zwatcher_is_active,
};

flux_watcher_t *zmqutil_watcher_create (flux_reactor_t *r,
                                        void *zsock,
                                        int events,
                                        flux_watcher_f cb,
                                        void *arg)
{
    struct zwatcher *zw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*zw), &zwatcher_ops, cb, arg)))
        return NULL;
    zw = watcher_get_data (w);
    zw->events = events | FLUX_POLLERR;
    zw->zsock = zsock;

    if (!(zw->prepare_w = flux_prepare_watcher_create (r, prepare_cb, w))
        || !(zw->check_w = flux_check_watcher_create (r, check_cb, w))
        || !(zw->idle_w = flux_idle_watcher_create (r, NULL, NULL)))
        goto error;

    int fd;
    if (get_zmq_fd (zsock, &fd) < 0
        || !(zw->fd_w = flux_fd_watcher_create (r, fd, FLUX_POLLIN, fd_cb, w)))
        goto error;
    return w;
error:
    ERRNO_SAFE_WRAP (flux_watcher_destroy, w);
    return NULL;
}

void *zmqutil_watcher_get_zsock (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &zwatcher_ops) {
        errno = EINVAL;
        return NULL;
    }
    struct zwatcher *zw = watcher_get_data (w);
    return zw->zsock;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

