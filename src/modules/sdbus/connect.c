/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* connect.c - connect to sd-bus with retries
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#ifndef HAVE_STRLCPY
#include "src/common/libmissing/strlcpy.h"
#endif
#include <flux/core.h>
#include <systemd/sd-bus.h>

#include "connect.h"

struct sdconnect {
    flux_t *h;
    int attempt;
    double retry_min;
    double retry_max;
    bool first_time;
    bool system_bus;
};

static void sdconnect_destroy (struct sdconnect *sdc)
{
    if (sdc) {
        int saved_errno = errno;
        free (sdc);
        errno = saved_errno;
    }
}

static struct sdconnect *sdconnect_create (void)
{
    struct sdconnect *sdc;

    if (!(sdc = calloc (1, sizeof (*sdc))))
        return NULL;
    return sdc;
}


static void bus_destroy (sd_bus *bus)
{
    if (bus) {
        int saved_errno = errno;
        sd_bus_flush (bus);
        sd_bus_close (bus);
        sd_bus_unref (bus);
        errno = saved_errno;
    }
}

static void make_system_bus_path (char *buf, size_t size)
{
    char *path;

    if ((path = getenv ("DBUS_SYSTEM_BUS_ADDRESS")))
        strlcpy (buf, path, size);
    else
        strlcpy (buf, "sd_bus_open_system", size);
}

static void make_user_bus_path (char *buf, size_t size)
{
    char *path;

    if ((path = getenv ("DBUS_SESSION_BUS_ADDRESS")))
        strlcpy (buf, path, size);
    else if ((path = getenv ("XDG_RUNTIME_DIR")))
        snprintf (buf, size, "unix:path:%s/bus", path);
    else
        strlcpy (buf, "sd_bus_open_user", size);
}

/* The timer callback calls sd_bus_open_user().  If it succeeds, the future
 * is fulfilled.  If it fails, the timer is re-armed for a calculated timeout.
 * Retries proceed forever.  If they need to be capped, this can be done by
 * specifying a flux_future_then() timeout.
 */
static void timer_cb (flux_reactor_t *r,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    flux_future_t *f = arg;
    struct sdconnect *sdc = flux_future_aux_get (f, "flux::sdc");
    sd_bus *bus;
    int e;
    double timeout;
    char path[1024];

    sdc->attempt++;
    timeout = sdc->retry_min * sdc->attempt;
    if (timeout > sdc->retry_max)
        timeout = sdc->retry_max;

    if (sdc->system_bus) {
        make_system_bus_path (path, sizeof (path));
        e = sd_bus_open_system (&bus);
    }
    else {
        make_user_bus_path (path, sizeof (path));
        e = sd_bus_open_user (&bus);
    }
    if (e < 0) {
        flux_log (sdc->h,
                  LOG_INFO,
                  "%s: %s (retrying in %.0fs)",
                  path,
                  strerror (-e),
                  timeout);
        goto retry;
    }
    flux_log (sdc->h, LOG_INFO, "%s: connected", path);
    flux_future_fulfill (f, bus, (flux_free_f)bus_destroy);
    sdc->attempt = 0;
    return;
retry:
    flux_timer_watcher_reset (w, timeout, 0.);
    flux_watcher_start (w);
    return;
}

/* This function is called when a future returned by sdbus_connect() is
 * passed to flux_future_get() or flux_future_then().  It starts the connect
 * timer, which fires immediately if first_time=true; otherwise in retry_min
 * seconds.
 */
static void initialize_cb (flux_future_t *f, void *arg)
{
    struct sdconnect *sdc = flux_future_aux_get (f, "flux::sdc");
    flux_reactor_t *r = flux_future_get_reactor (f);
    flux_watcher_t *w;
    double timeout = sdc->first_time ? 0. : sdc->retry_min;

    if (!(w = flux_timer_watcher_create (r, timeout, 0., timer_cb, f))
        || flux_future_aux_set (f,
                                NULL,
                                w,
                                (flux_free_f)flux_watcher_destroy) < 0) {
        flux_watcher_destroy (w);
        goto error;
    }
    flux_watcher_start (w);
    return;
error:
    flux_future_fulfill_error (f, errno, NULL);
}

/* sdbus_connect() returns to the caller without interacting with sd-bus.
 * The action begins when the user calls flux_future_then() et al on
 * the returned future.
 */
flux_future_t *sdbus_connect (flux_t *h,
                              bool first_time,
                              double retry_min,
                              double retry_max,
                              bool system_bus)
{
    flux_future_t *f;
    struct sdconnect *sdc = NULL;

    if (!(f = flux_future_create (initialize_cb, NULL))
        || !(sdc = sdconnect_create ())
        || flux_future_aux_set (f,
                                "flux::sdc",
                                sdc,
                                (flux_free_f)sdconnect_destroy) < 0) {
        sdconnect_destroy (sdc);
        goto error;
    }
    sdc->h = h;
    sdc->retry_min = retry_min;
    sdc->retry_max = retry_max;
    sdc->first_time = first_time;
    sdc->system_bus = system_bus;
    flux_future_set_flux (f, h);
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

// vi:tabstop=4 shiftwidth=4 expandtab
