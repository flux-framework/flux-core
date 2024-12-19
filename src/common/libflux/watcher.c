/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <flux/core.h>

#include "reactor_private.h"
#include "watcher_private.h"

struct flux_watcher {
    flux_reactor_t *r;
    flux_watcher_f fn;
    void *arg;
    struct flux_watcher_ops *ops;
    bool unreferenced;
    void *data;
};

flux_watcher_t *watcher_create (flux_reactor_t *r,
                                size_t data_size,
                                struct flux_watcher_ops *ops,
                                flux_watcher_f fn,
                                void *arg)
{
    struct flux_watcher *w = calloc (1, sizeof (*w) + data_size);
    if (!w)
        return NULL;
    w->r = r;
    w->ops = ops;
    w->data = w + 1;
    w->fn = fn;
    w->arg = arg;
    flux_reactor_incref (r);
    return w;
}

void *watcher_get_data (flux_watcher_t *w)
{
    if (w)
        return w->data;
    return NULL;
}

struct flux_watcher_ops *watcher_get_ops (flux_watcher_t *w)
{
    if (w)
        return w->ops;
    return NULL;
}

void watcher_call (flux_watcher_t *w, int revents)
{
    if (w->fn)
        w->fn (w->r, w, revents, w->arg);
}

void *watcher_get_arg (flux_watcher_t *w)
{
    if (w)
        return w->arg;
    return NULL;
}

flux_reactor_t *watcher_get_reactor (flux_watcher_t *w)
{
    return w ? w->r : NULL;
}

void flux_watcher_set_priority (flux_watcher_t *w, int priority)
{
    if (w) {
        if (w->ops->set_priority)
            w->ops->set_priority (w, priority);
    }
}

void flux_watcher_start (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->start)
            w->ops->start (w);
    }
}

void flux_watcher_stop (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->stop)
            w->ops->stop (w);
    }
}

void flux_watcher_ref (flux_watcher_t *w)
{
    if (w && w->unreferenced) {
        if (w->ops->ref) {
            w->ops->ref (w);
            w->unreferenced = false;
        }
    }
}

void flux_watcher_unref (flux_watcher_t *w)
{
    if (w && !w->unreferenced) {
        if (w->ops->unref) {
            w->ops->unref(w);
            w->unreferenced = true;
        }
    }
}

bool flux_watcher_is_active (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->is_active)
            return w->ops->is_active (w);
    }
    return false;
}

bool flux_watcher_is_referenced (flux_watcher_t *w)
{
    if (w)
        return !w->unreferenced;
    return true;
}

void flux_watcher_destroy (flux_watcher_t *w)
{
    if (w) {
        if (w->ops->stop)
            w->ops->stop (w);
        if (w->ops->destroy)
            w->ops->destroy (w);
        flux_reactor_decref (w->r);
        free (w);
    }
}

// vi:ts=4 sw=4 expandtab
