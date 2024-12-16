/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watcher.c - a flux watcher that becomes ready when sd-bus needs service
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <math.h>
#include <systemd/sd-bus.h>
#include <flux/core.h>

#include "src/common/libflux/watcher_private.h"

#include "watcher.h"

struct sdbus_watcher {
    sd_bus *bus;
    flux_watcher_t *in;
    flux_watcher_t *out;
    flux_watcher_t *tmout;
    flux_watcher_t *prep;

    flux_watcher_t *w;

    flux_watcher_f cb;
    void *cb_arg;
};

/* The event loop is about to (possibly) block.  The job of this function
 * is to ensure that the appropriate watchers are enabled so the event loop
 * unblocks when sd-bus requires service.
 * N.B. in practice, it seems that sd_bus_get_events always returns() at
 * least POLLIN, which makes sense given that the D-Bus spec allows the bus
 * to send unsolicited signals like 'NameAcquired'.
 */
static void prep_cb (flux_reactor_t *r,
                     flux_watcher_t *w,
                     int revents,
                     void *arg)
{
    struct sdbus_watcher *sdw = arg;
    int events;
    uint64_t usec;
    struct timespec ts;

    flux_watcher_stop (sdw->in);
    flux_watcher_stop (sdw->out);
    flux_watcher_stop (sdw->tmout);

    if ((events = sd_bus_get_events (sdw->bus)) >= 0) {
        if ((events & POLLIN))
            flux_watcher_start (sdw->in);
        if ((events & POLLOUT))
            flux_watcher_start (sdw->out);
    }
    /* sd_bus_get_timeout(3) sets 'usec' to the absolute time when the bus
     * wants service, or UINT64_MAX for "no timeout".  Convert that to a time
     * relative to now wanted by the flux timer watcher.
     * N.B.  floor() rounds 'now' down so that when it is subtracted from
     * 'usec', the result is rounded up per sd_bus_get_timeout(3)
     * recommendation.
     */
    if (sd_bus_get_timeout (sdw->bus, &usec) >= 0
        && usec != UINT64_MAX
        && clock_gettime (CLOCK_MONOTONIC, &ts) == 0) {
        double now = floor (1E6*ts.tv_sec + 1E-3*ts.tv_nsec);
        double timeout = 1E-6*(usec - now);

        if (timeout >= 0.) {
            flux_timer_watcher_reset (sdw->tmout, timeout, 0.);
            flux_watcher_start (sdw->tmout);
        }
    }
}

/* The timer and/or fd watchers are ready.  Call the bus watcher callback
 * so it can call sd_bus_process(3).
 */
static void bus_cb (flux_reactor_t *r,
                    flux_watcher_t *w,
                    int revents,
                    void *arg)
{
    struct sdbus_watcher *sdw = arg;

    if (sdw->cb)
        sdw->cb (r, sdw->w, revents, sdw->cb_arg);
}

static void op_start (flux_watcher_t *w)
{
    struct sdbus_watcher *sdw = watcher_get_data (w);
    flux_watcher_start (sdw->prep);
}

static void op_stop (flux_watcher_t *w)
{
    struct sdbus_watcher *sdw = watcher_get_data (w);
    flux_watcher_stop (sdw->prep);
    flux_watcher_stop (sdw->in);
    flux_watcher_stop (sdw->out);
    flux_watcher_stop (sdw->tmout);
}

static bool op_is_active (flux_watcher_t *w)
{
    struct sdbus_watcher *sdw = watcher_get_data (w);
    return flux_watcher_is_active (sdw->prep);
}

static void op_destroy (flux_watcher_t *w)
{
    struct sdbus_watcher *sdw = watcher_get_data (w);
    flux_watcher_destroy (sdw->prep);
    flux_watcher_destroy (sdw->in);
    flux_watcher_destroy (sdw->out);
    flux_watcher_destroy (sdw->tmout);
}

static struct flux_watcher_ops sdbus_watcher_ops = {
    .start = op_start,
    .stop = op_stop,
    .destroy = op_destroy,
    .is_active = op_is_active,
};

flux_watcher_t *sdbus_watcher_create (flux_reactor_t *r,
                                      sd_bus *bus,
                                      flux_watcher_f cb,
                                      void *arg)
{
    flux_watcher_t *w;
    struct sdbus_watcher *sdw;
    int fd;

    if ((fd = sd_bus_get_fd (bus)) < 0) {
        errno = -fd;
        return NULL;
    }
    if (!(w = watcher_create (r,
                              sizeof (*sdw),
                              &sdbus_watcher_ops,
                              cb,
                              arg)))
        return NULL;
    sdw = watcher_get_data (w);
    sdw->bus = bus;
    sdw->w = w;
    sdw->cb = cb;
    sdw->cb_arg = arg;
    if (!(sdw->out = flux_fd_watcher_create (r, fd, FLUX_POLLOUT, bus_cb, sdw))
        || !(sdw->in = flux_fd_watcher_create (r, fd, FLUX_POLLIN, bus_cb, sdw))
        || !(sdw->tmout = flux_timer_watcher_create (r, 0., 0., bus_cb, sdw))
        || !(sdw->prep = flux_prepare_watcher_create (r, prep_cb, sdw)))
        goto error;
    return w;
error:
    flux_watcher_destroy (w);
    return NULL;
}


// vi:tabstop=4 shiftwidth=4 expandtab
