/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* private interfaces for "subclassing" watcher.c */

#ifndef _FLUX_CORE_WATCHER_PRIVATE_H
#define _FLUX_CORE_WATCHER_PRIVATE_H

#include "reactor.h"

struct flux_watcher_ops {
    void (*set_priority) (flux_watcher_t *w, int priority);
    void (*start) (flux_watcher_t *w);
    void (*stop) (flux_watcher_t *w);
    void (*destroy) (flux_watcher_t *w);
    bool (*is_active) (flux_watcher_t *w);
};

/*  Create a custom watcher on reactor 'r' with 'data_size' bytes reserved
 *   for the implementor, implementation operations in 'ops' and user
 *   watcher callback and data 'fn' and 'arg'.
 *
 *  Caller retrieves pointer to allocated implementation data with
 *   flux_watcher_data (w).
 */
flux_watcher_t *watcher_create (flux_reactor_t *r,
                                size_t data_size,
                                struct flux_watcher_ops *ops,
                                flux_watcher_f fn,
                                void *arg);

/*  Return pointer to implementation data reserved by watcher object 'w'.
 */
void *watcher_get_data (flux_watcher_t *w);

/*  Return pointer to flux_watcher_ops structure for this watcher.
 */
struct flux_watcher_ops *watcher_get_ops (flux_watcher_t *w);

void watcher_call (flux_watcher_t *w, int revents);

flux_reactor_t *watcher_get_reactor (flux_watcher_t *w);

void *watcher_get_arg (flux_watcher_t *w);

#endif /* !_FLUX_CORE_WATCHER_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
