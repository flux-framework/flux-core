/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _EV_FLUX_H
#define _EV_FLUX_H

#include "src/common/libev/ev.h"

struct ev_flux;

typedef void (*ev_flux_f) (struct ev_loop *loop, struct ev_flux *w, int revents);

struct ev_flux {
    ev_io io_w;
    ev_prepare prepare_w;
    ev_idle idle_w;
    ev_check check_w;
    flux_t *h;
    int pollfd;
    int events;
    ev_flux_f cb;
    void *data;
};

int ev_flux_init (struct ev_flux *w, ev_flux_f cb, flux_t *h, int events);
void ev_flux_start (struct ev_loop *loop, struct ev_flux *w);
void ev_flux_stop (struct ev_loop *loop, struct ev_flux *w);

#endif /* !_EV_FLUX_H */
