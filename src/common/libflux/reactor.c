/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <czmq.h>

#include "handle.h"
#include "reactor.h"
#include "ev_flux.h"

#include "src/common/libev/ev.h"
#include "src/common/libutil/ev_zmq.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"

struct flux_reactor {
    struct ev_loop *loop;
    int loop_rc;
};

struct watcher_ops {
    void (*start)(void *impl, flux_reactor_t *r, flux_watcher_t *w);
    void (*stop)(void *impl, flux_reactor_t *r, flux_watcher_t *w);
    void (*destroy)(void *impl, flux_watcher_t *w);
};

struct flux_watcher {
    flux_watcher_f fn;
    void *arg;
    int signature;
    struct watcher_ops ops;
    void *impl;
};

void flux_reactor_destroy (flux_reactor_t *r)
{
    if (r) {
        if (r->loop)
            ev_loop_destroy (r->loop);
        free (r);
    }
}

flux_reactor_t *flux_reactor_create (void)
{
    flux_reactor_t *r = xzmalloc (sizeof (*r));
    r->loop = ev_loop_new (EVFLAG_AUTO);
    if (!r->loop) {
        errno = ENOMEM;
        flux_reactor_destroy (r);
        return NULL;
    }
    ev_set_userdata (r->loop, r);
    return r;
}

void flux_set_reactor (flux_t h, flux_reactor_t *r)
{
    flux_aux_set (h, "flux::reactor", r, NULL);
}

flux_reactor_t *flux_get_reactor (flux_t h)
{
    flux_reactor_t *r = flux_aux_get (h, "flux::reactor");
    if (!r) {
        if ((r = flux_reactor_create ()))
            flux_aux_set (h, "flux::reactor", r,
                          (flux_free_f)flux_reactor_destroy);
    }
    return r;
}

int flux_reactor_run (flux_reactor_t *r, int flags)
{
    int ev_flags = 0;
    int count;
    if (flags & FLUX_REACTOR_NOWAIT)
        ev_flags |= EVRUN_NOWAIT;
    if (flags & FLUX_REACTOR_ONCE)
        ev_flags |= EVRUN_ONCE;
    r->loop_rc = 0;
    count = ev_run (r->loop, ev_flags);
    if (count > 0 && r->loop_rc == 0 && ((flags & FLUX_REACTOR_NOWAIT)
                                     || (flags & FLUX_REACTOR_ONCE))) {
        errno = EWOULDBLOCK;
        return -1;
    }
    return r->loop_rc;
}

void flux_reactor_stop (flux_reactor_t *r)
{
    r->loop_rc = 0;
    ev_break (r->loop, EVBREAK_ALL);
}

void flux_reactor_stop_error (flux_reactor_t *r)
{
    r->loop_rc = -1;
    ev_break (r->loop, EVBREAK_ALL);
}

static int events_to_libev (int events)
{
    int e = 0;
    if (events & FLUX_POLLIN)
        e |= EV_READ;
    if (events & FLUX_POLLOUT)
        e |= EV_WRITE;
    if (events & FLUX_POLLERR)
        e |= EV_ERROR;
    return e;
}

static int libev_to_events (int events)
{
    int e = 0;
    if (events & EV_READ)
        e |= FLUX_POLLIN;
    if (events & EV_WRITE)
        e |= FLUX_POLLOUT;
    if (events & EV_ERROR)
        e |= FLUX_POLLERR;
    return e;
}

/**
 ** Watchers
 **/

static flux_watcher_t *flux_watcher_create (void *impl, struct watcher_ops ops,
                                            int signature,
                                            flux_watcher_f fun, void *arg)
{
    struct flux_watcher *w = xzmalloc (sizeof (*w));
    w->ops = ops;
    w->signature = signature;
    w->impl = impl;
    w->fn = fun;
    w->arg = arg;
    return w;
}

void flux_watcher_start (flux_reactor_t *r, flux_watcher_t *w)
{
    if (w) {
        if (w->ops.start)
            w->ops.start (w->impl, r, w);
    }
}

void flux_watcher_stop (flux_reactor_t *r, flux_watcher_t *w)
{
    if (w) {
        if (w->ops.stop)
            w->ops.stop (w->impl, r, w);
    }
}

void flux_watcher_destroy (flux_watcher_t *w)
{
    if (w) {
        if (w->ops.destroy)
            w->ops.destroy (w->impl, w);
        free (w);
    }
}

/* flux_t handle
 */

#define HANDLE_SIG 1006

static void handle_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == HANDLE_SIG);
    ev_flux_start (r->loop, (ev_flux *)impl);
}

static void handle_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == HANDLE_SIG);
    ev_flux_stop (r->loop, (ev_flux *)impl);
}

static void handle_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == HANDLE_SIG);
    if (impl)
        free (impl);
}

static void handle_cb (struct ev_loop *loop, ev_flux *fw, int revents)
{
    struct flux_watcher *w = fw->data;
    assert (w->signature == HANDLE_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_handle_watcher_create (flux_t h, int events,
                                            flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = handle_start,
        .stop = handle_stop,
        .destroy = handle_destroy,
    };
    ev_flux *fw = xzmalloc (sizeof (*fw));
    flux_watcher_t *w;

    ev_flux_init (fw, handle_cb, h, events_to_libev (events) & ~EV_ERROR);
    w = flux_watcher_create (fw, ops, HANDLE_SIG, cb, arg);
    fw->data = w;

    return w;
}

flux_t flux_handle_watcher_get_flux (flux_watcher_t *w)
{
    assert (w->signature == HANDLE_SIG);
    ev_flux *fw = w->impl;
    return fw->h;
}

/* file descriptors
 */

#define FD_SIG 1005

static void fd_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == FD_SIG);
    ev_io_start (r->loop, (ev_io *)impl);
}

static void fd_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == FD_SIG);
    ev_io_stop (r->loop, (ev_io *)impl);
}

static void fd_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == FD_SIG);
    if (impl)
        free (impl);
}

static void fd_cb (struct ev_loop *loop, ev_io *iow, int revents)
{
    struct flux_watcher *w = iow->data;
    assert (w->signature == FD_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_fd_watcher_create (int fd, int events,
                                        flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = fd_start,
        .stop = fd_stop,
        .destroy = fd_destroy,
    };
    ev_io *iow = xzmalloc (sizeof (*iow));
    flux_watcher_t *w;

    ev_io_init (iow, fd_cb, fd, events_to_libev (events) & ~EV_ERROR);
    w = flux_watcher_create (iow, ops, FD_SIG, cb, arg);
    iow->data = w;

    return w;
}

int flux_fd_watcher_get_fd (flux_watcher_t *w)
{
    assert (w->signature == FD_SIG);
    ev_io *iow = w->impl;
    return iow->fd;
}

/* 0MQ sockets
 */

#define ZMQ_SIG 1004

static void zmq_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == ZMQ_SIG);
    ev_zmq_start (r->loop, (ev_zmq *)impl);
}

static void zmq_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == ZMQ_SIG);
    ev_zmq_stop (r->loop, (ev_zmq *)impl);
}

static void zmq_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == ZMQ_SIG);
    if (impl)
        free (impl);
}

static void zmq_cb (struct ev_loop *loop, ev_zmq *pw, int revents)
{
    struct flux_watcher *w = pw->data;
    assert (w->signature == ZMQ_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_zmq_watcher_create (void *zsock, int events,
                                         flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = zmq_start,
        .stop = zmq_stop,
        .destroy = zmq_destroy,
    };
    ev_zmq *zw = xzmalloc (sizeof (*zw));
    flux_watcher_t *w;

    ev_zmq_init (zw, zmq_cb, zsock, events_to_libev (events) & ~EV_ERROR);
    w = flux_watcher_create (zw, ops, ZMQ_SIG, cb, arg);
    zw->data = w;

    return w;
}

void *flux_zmq_watcher_get_zsock (flux_watcher_t *w)
{
    assert (w->signature == ZMQ_SIG);
    ev_zmq *zw = w->impl;
    return zw->zsock;
}

/* Timer
 */

#define TIMER_SIG 1003

static void timer_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == TIMER_SIG);
    ev_timer_start (r->loop, (ev_timer *)impl);
}

static void timer_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == TIMER_SIG);
    ev_timer_stop (r->loop, (ev_timer *)impl);
}

static void timer_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == TIMER_SIG);
    if (impl)
        free (impl);
}

static void timer_cb (struct ev_loop *loop, ev_timer *tw, int revents)
{
    struct flux_watcher *w = tw->data;
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_timer_watcher_create (double after, double repeat,
                                           flux_watcher_f cb, void *arg)
{
    if (after < 0 || repeat < 0) {
        errno = EINVAL;
        return NULL;
    }
    struct watcher_ops ops = {
        .start = timer_start,
        .stop = timer_stop,
        .destroy = timer_destroy,
    };
    ev_timer *tw = xzmalloc (sizeof (*tw));
    flux_watcher_t *w;

    ev_timer_init (tw, timer_cb, after, repeat);
    w = flux_watcher_create (tw, ops, TIMER_SIG, cb, arg);
    tw->data = w;

    return w;
}

void flux_timer_watcher_reset (flux_watcher_t *w, double after, double repeat)
{
    assert (w->signature == TIMER_SIG);
    ev_timer *tw = w->impl;
    ev_timer_set (tw, after, repeat);
}

/* Prepare
 */
#define PREPARE_SIG 1002

static void prepare_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == PREPARE_SIG);
    ev_prepare_start (r->loop, (ev_prepare *)impl);
}

static void prepare_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == PREPARE_SIG);
    ev_prepare_stop (r->loop, (ev_prepare *)impl);
}

static void prepare_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == PREPARE_SIG);
    if (impl)
        free (impl);
}

static void prepare_cb (struct ev_loop *loop, ev_prepare *pw, int revents)
{
    struct flux_watcher *w = pw->data;
    assert (w->signature == PREPARE_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_prepare_watcher_create (flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = prepare_start,
        .stop = prepare_stop,
        .destroy = prepare_destroy,
    };
    ev_prepare *pw = xzmalloc (sizeof (*pw));
    flux_watcher_t *w;

    ev_prepare_init (pw, prepare_cb);
    w = flux_watcher_create (pw, ops, PREPARE_SIG, cb, arg);
    pw->data = w;

    return w;
}

/* Check
 */

#define CHECK_SIG 1001

static void check_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == CHECK_SIG);
    ev_check_start (r->loop, (ev_check *)impl);
}

static void check_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == CHECK_SIG);
    ev_check_stop (r->loop, (ev_check *)impl);
}

static void check_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == CHECK_SIG);
    if (impl)
        free (impl);
}

static void check_cb (struct ev_loop *loop, ev_check *cw, int revents)
{
    struct flux_watcher *w = cw->data;
    assert (w->signature == CHECK_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_check_watcher_create (flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = check_start,
        .stop = check_stop,
        .destroy = check_destroy,
    };
    ev_check *cw = xzmalloc (sizeof (*cw));
    flux_watcher_t *w;

    ev_check_init (cw, check_cb);
    w = flux_watcher_create (cw, ops, CHECK_SIG, cb, arg);
    cw->data = w;

    return w;
}

/* Idle
 */

#define IDLE_SIG 1000

static void idle_start (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == IDLE_SIG);
    ev_idle_start (r->loop, (ev_idle *)impl);
}

static void idle_stop (void *impl, flux_reactor_t *r, flux_watcher_t *w)
{
    assert (w->signature == IDLE_SIG);
    ev_idle_stop (r->loop, (ev_idle *)impl);
}

static void idle_destroy (void *impl, flux_watcher_t *w)
{
    assert (w->signature == IDLE_SIG);
    if (impl)
        free (impl);
}

static void idle_cb (struct ev_loop *loop, ev_idle *iw, int revents)
{
    struct flux_watcher *w = iw->data;
    assert (w->signature == IDLE_SIG);
    if (w->fn)
        w->fn (ev_userdata (loop), w, libev_to_events (revents), w->arg);
}

flux_watcher_t *flux_idle_watcher_create (flux_watcher_f cb, void *arg)
{
    struct watcher_ops ops = {
        .start = idle_start,
        .stop = idle_stop,
        .destroy = idle_destroy,
    };
    ev_idle *iw = xzmalloc (sizeof (*iw));
    flux_watcher_t *w;

    ev_idle_init (iw, idle_cb);
    w = flux_watcher_create (iw, ops, IDLE_SIG, cb, arg);
    iw->data = w;

    return w;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
