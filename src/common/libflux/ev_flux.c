/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <zmq.h>
#include <stdbool.h>

#include "handle.h"

#include "src/common/libev/ev.h"

#include "ev_flux.h"

/* Get flux poll events, converted to libev
 */
static int get_pollevents (flux_t *h)
{
    int e = flux_pollevents (h);
    int events = 0;
    if (e < 0 || (e & FLUX_POLLERR))
        events |= EV_ERROR;
    if ((e & FLUX_POLLIN))
        events |= EV_READ;
    if ((e & FLUX_POLLOUT))
        events |= EV_WRITE;
    return events;
}

static void prepare_cb (struct ev_loop *loop, ev_prepare *w, int revents)
{
    struct ev_flux *fw =
        (struct ev_flux *)((char *)w - offsetof (struct ev_flux, prepare_w));
    int events = get_pollevents (fw->h);

    if ((events & fw->events) || (events & EV_ERROR))
        ev_idle_start (loop, &fw->idle_w);
    else
        ev_io_start (loop, &fw->io_w);
}

static void check_cb (struct ev_loop *loop, ev_check *w, int revents)
{
    struct ev_flux *fw =
        (struct ev_flux *)((char *)w - offsetof (struct ev_flux, check_w));
    int events = get_pollevents (fw->h);

    ev_io_stop (loop, &fw->io_w);
    ev_idle_stop (loop, &fw->idle_w);

    if ((events & fw->events) || (events & EV_ERROR))
        fw->cb (loop, fw, events);
}

int ev_flux_init (struct ev_flux *w, ev_flux_f cb, flux_t *h, int events)
{
    w->cb = cb;
    w->h = h;
    w->events = events;
    if ((w->pollfd = flux_pollfd (h)) < 0)
        return -1;

    ev_prepare_init (&w->prepare_w, prepare_cb);
    ev_check_init (&w->check_w, check_cb);
    ev_idle_init (&w->idle_w, NULL);
    ev_io_init (&w->io_w, NULL, w->pollfd, EV_READ);

    return 0;
}

void ev_flux_start (struct ev_loop *loop, struct ev_flux *w)
{
    ev_prepare_start (loop, &w->prepare_w);
    ev_check_start (loop, &w->check_w);
}

void ev_flux_stop (struct ev_loop *loop, struct ev_flux *w)
{
    ev_prepare_stop (loop, &w->prepare_w);
    ev_check_stop (loop, &w->check_w);
    ev_io_stop (loop, &w->io_w);
    ev_idle_stop (loop, &w->idle_w);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
