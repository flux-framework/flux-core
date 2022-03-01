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
    bool is_active;
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

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_REACTOR_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
