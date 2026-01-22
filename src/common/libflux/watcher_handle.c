/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* watcher_handle.c - reactor watcher for flux_t handle */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "watcher_private.h"

struct handle_watcher {
    flux_watcher_t *fd_w;
    flux_watcher_t *prepare_w;
    flux_watcher_t *idle_w;
    flux_watcher_t *check_w;
    flux_t *h;
    int events;
};

static void handle_watcher_start (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);

    flux_watcher_start (hw->prepare_w);
    flux_watcher_start (hw->check_w);
}

static void handle_watcher_stop (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);

    flux_watcher_stop (hw->prepare_w);
    flux_watcher_stop (hw->check_w);
    flux_watcher_stop (hw->fd_w);
    flux_watcher_stop (hw->idle_w);
}

static void handle_watcher_ref (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);

    flux_watcher_ref (hw->fd_w);
    flux_watcher_ref (hw->prepare_w);
    flux_watcher_ref (hw->idle_w);
    flux_watcher_ref (hw->check_w);
}

static void handle_watcher_unref (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);

    flux_watcher_unref (hw->fd_w);
    flux_watcher_unref (hw->prepare_w);
    flux_watcher_unref (hw->idle_w);
    flux_watcher_unref (hw->check_w);
}

static bool handle_watcher_is_active (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);

    return flux_watcher_is_active (hw->prepare_w);
}

static void handle_watcher_destroy (flux_watcher_t *w)
{
    struct handle_watcher *hw = watcher_get_data (w);
    if (hw) {
        flux_watcher_destroy (hw->prepare_w);
        flux_watcher_destroy (hw->check_w);
        flux_watcher_destroy (hw->fd_w);
        flux_watcher_destroy (hw->idle_w);
    }
}

static void handle_watcher_prepare_cb (flux_reactor_t *r,
                                       flux_watcher_t *prepare_w,
                                       int prepare_revents,
                                       void *arg)
{
    flux_watcher_t *w = arg;
    struct handle_watcher *hw = watcher_get_data (w);
    int hevents;

    if ((hevents = flux_pollevents (hw->h)) < 0)
        hevents = FLUX_POLLERR;

    if ((hevents & hw->events))
        flux_watcher_start (hw->idle_w);
    else
        flux_watcher_start (hw->fd_w);
}

static void handle_watcher_check_cb (flux_reactor_t *r,
                                     flux_watcher_t *check_w,
                                     int check_revents,
                                     void *arg)
{
    flux_watcher_t *w = arg;
    struct handle_watcher *hw = watcher_get_data (w);
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

static struct flux_watcher_ops handle_watcher_ops = {
    .start = handle_watcher_start,
    .stop = handle_watcher_stop,
    .ref = handle_watcher_ref,
    .unref = handle_watcher_unref,
    .is_active = handle_watcher_is_active,
    .destroy = handle_watcher_destroy,
};

flux_watcher_t *flux_handle_watcher_create (flux_reactor_t *r,
                                            flux_t *h,
                                            int events,
                                            flux_watcher_f cb,
                                            void *arg)
{
    struct handle_watcher *hw;
    flux_watcher_t *w;
    if (!(w = watcher_create (r, sizeof (*hw), &handle_watcher_ops, cb, arg)))
        return NULL;
    hw = watcher_get_data (w);
    hw->events = events | FLUX_POLLERR;
    hw->h = h;

    hw->prepare_w = flux_prepare_watcher_create (r,
                                                 handle_watcher_prepare_cb,
                                                 w);
    if (!hw->prepare_w
        || !(hw->check_w = flux_check_watcher_create (r,
                                                      handle_watcher_check_cb,
                                                      w))
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
    if (watcher_get_ops (w) != &handle_watcher_ops) {
        errno = EINVAL;
        return NULL;
    }
    struct handle_watcher *hw = watcher_get_data (w);
    return hw->h;
}

// vi:ts=4 sw=4 expandtab
