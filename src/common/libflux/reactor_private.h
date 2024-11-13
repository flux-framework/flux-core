/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_REACTOR_PRIVATE_H
#define _FLUX_CORE_REACTOR_PRIVATE_H

#include "src/common/libev/ev.h"
#include "reactor.h"

#ifdef __cplusplus
extern "C" {
#endif

struct flux_watcher_ops {
    void (*set_priority) (flux_watcher_t *w, int priority);
    void (*start) (flux_watcher_t *w);
    void (*stop) (flux_watcher_t *w);
    void (*destroy) (flux_watcher_t *w);
};

struct flux_reactor {
    struct ev_loop *loop;
    int usecount;
    unsigned int errflag:1;
};

struct flux_watcher {
    flux_reactor_t *r;
    flux_watcher_f fn;
    void *arg;
    struct flux_watcher_ops *ops;
    void *data;
};

static inline int events_to_libev (int events)
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

static inline int libev_to_events (int events)
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

/*  Create a custom watcher on reactor 'r' with 'data_size' bytes reserved
 *   for the implementor, implementation operations in 'ops' and user
 *   watcher callback and data 'fn' and 'arg'.
 *
 *  Caller retrieves pointer to allocated implementation data with
 *   flux_watcher_data (w).
 */
static inline flux_watcher_t *watcher_create (flux_reactor_t *r,
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

/*  Return pointer to implementation data reserved by watcher object 'w'.
 */
static inline void *watcher_get_data (flux_watcher_t *w)
{
    if (w)
        return w->data;
    return NULL;
}

/*  Return pointer to flux_watcher_ops structure for this watcher.
 */
static inline struct flux_watcher_ops *watcher_get_ops (flux_watcher_t *w)
{
    if (w)
        return w->ops;
    return NULL;
}


#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_REACTOR_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
