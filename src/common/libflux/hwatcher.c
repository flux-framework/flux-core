/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* hwatcher.c - reactor watcher for flux_t handle */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "watcher_private.h"

struct hwatcher {
    flux_watcher_t *fd_w;
    flux_watcher_t *prepare_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    flux_t *h;
    int events;
};

static void hwatcher_start (flux_watcher_t *w)
{
    struct hwatcher *hw = watcher_get_data (w);

    flux_watcher_start (hw->prepare_w);
    flux_watcher_start (hw->check_w);
}

static void hwatcher_stop (flux_watcher_t *w)
{
    struct hwatcher *hw = watcher_get_data (w);

    flux_watcher_stop (hw->prepare_w);
    flux_watcher_stop (hw->check_w);
    flux_watcher_stop (hw->fd_w);
    flux_watcher_stop (hw->idle_w);
}

static bool hwatcher_is_active (flux_watcher_t *w)
{
    struct hwatcher *hw = watcher_get_data (w);

    return flux_watcher_is_active (hw->prepare_w);
}

static void hwatcher_destroy (flux_watcher_t *w)
{
    struct hwatcher *hw = watcher_get_data (w);
    if (hw) {
        flux_watcher_destroy (hw->prepare_w);
        flux_watcher_destroy (hw->check_w);
        flux_watcher_destroy (hw->fd_w);
        flux_watcher_destroy (hw->idle_w);
    }
}

static void hwatcher_prepare_cb (flux_reactor_t *r,
                                 flux_watcher_t *prepare_w,
                                 int prepare_revents,
                                 void *arg)
{
    flux_watcher_t *w = arg;
    struct hwatcher *hw = watcher_get_data (w);
    int hevents;

    if ((hevents = flux_pollevents (hw->h)) < 0)
        hevents = FLUX_POLLERR;

    if ((hevents & hw->events))
        flux_watcher_start (hw->idle_w);
    else
        flux_watcher_start (hw->fd_w);
}

static void hwatcher_check_cb (flux_reactor_t *r,
                               flux_watcher_t *check_w,
                               int check_revents,
                               void *arg)
{
    flux_watcher_t *w = arg;
    struct hwatcher *hw = watcher_get_data (w);
    int hevents;
    int revents;

    flux_watcher_stop (hw->fd_w);
    flux_watcher_stop (hw->idle_w);

    if ((hevents = flux_pollevents (hw->h)) < 0)
        hevents = FLUX_POLLERR;
    revents = (hevents & hw->events);

    if (revents)
        watcher_call (w, revents);
}

static struct flux_watcher_ops hwatcher_ops = {
    .start = hwatcher_start,
    .stop = hwatcher_stop,
    .is_active = hwatcher_is_active,
    .destroy = hwatcher_destroy,
};

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t *h,
                                            int events,
                                            flux_watcher_f cb,
                                            void *arg)
{
    struct hwatcher *hw;
    flux_watcher_t *w;
    if (!(w = watcher_create (r, sizeof (*hw), &hwatcher_ops, cb, arg)))
        return NULL;
    hw = watcher_get_data (w);
    hw->events = events | FLUX_POLLERR;
    hw->h = h;

    if (!(hw->prepare_w = flux_prepare_watcher_create (r,
                                                       hwatcher_prepare_cb,
                                                       w))
        || !(hw->check_w = flux_check_watcher_create (r, hwatcher_check_cb, w))
        || !(hw->idle_w = flux_idle_watcher_create (r, NULL, NULL)))
        goto error;

    int fd;
    if ((fd = flux_pollfd (h)) < 0
        || !(hw->fd_w = flux_fd_watcher_create (r, fd, FLUX_POLLIN, NULL, w)))
        goto error;
    return w;
error:
    ERRNO_SAFE_WRAP (flux_watcher_destroy, w);
    return NULL;
}

flux_t *flux_handle_watcher_get_flux (flux_watcher_t *w)
{
    if (watcher_get_ops (w) != &hwatcher_ops) {
        errno = EINVAL;
        return NULL;
    }
    struct hwatcher *hw = watcher_get_data (w);
    return hw->h;
}

// vi:ts=4 sw=4 expandtab
