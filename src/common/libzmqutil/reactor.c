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
#include <zmq.h>
#include <flux/core.h>

#include "src/common/libev/ev.h"
#include "src/common/libflux/reactor_private.h"

#include "ev_zmq.h"
#include "reactor.h"

/* 0MQ sockets
 */

static void zmq_start (flux_watcher_t *w)
{
    ev_zmq_start (w->r->loop, (ev_zmq *)w->data);
}

static void zmq_stop (flux_watcher_t *w)
{
    ev_zmq_stop (w->r->loop, (ev_zmq *)w->data);
}

static bool zmq_is_active (flux_watcher_t *w)
{
    return ev_zmq_is_active (w->data);
}

static void zmq_cb (struct ev_loop *loop, ev_zmq *pw, int revents)
{
    struct flux_watcher *w = pw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

static struct flux_watcher_ops zmq_watcher  = {
    .start = zmq_start,
    .stop = zmq_stop,
    .destroy = NULL,
    .is_active = zmq_is_active,
};

flux_watcher_t *zmqutil_watcher_create (flux_reactor_t *r,
                                        void *zsock,
                                        int events,
                                        flux_watcher_f cb,
                                        void *arg)
{
    ev_zmq *zw;
    flux_watcher_t *w;

    if (!(w = watcher_create (r, sizeof (*zw), &zmq_watcher, cb, arg)))
        return NULL;
    zw = watcher_get_data (w);
    ev_zmq_init (zw, zmq_cb, zsock, events_to_libev (events) & ~EV_ERROR);
    zw->data = w;

    return w;
}

void *zmqutil_watcher_get_zsock (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &zmq_watcher) {
        errno = EINVAL;
        return NULL;
    }
    ev_zmq *zw = w->data;
    return zw->zsock;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

