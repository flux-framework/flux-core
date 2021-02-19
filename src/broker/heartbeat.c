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
#include <czmq.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/log.h"

#include "heartbeat.h"

struct heartbeat {
    flux_t *h;
    double rate;
    flux_watcher_t *timer;
};

static const double min_heartrate = 0.01;   /* min seconds */
static const double max_heartrate = 30;     /* max seconds */
static const double dfl_heartrate = 2;


void heartbeat_destroy (heartbeat_t *hb)
{
    if (hb) {
        flux_watcher_destroy (hb->timer);
        free (hb);
    }
}

heartbeat_t *heartbeat_create (void)
{
    heartbeat_t *hb = calloc (1, sizeof (*hb));

    if (!hb) {
        errno = ENOMEM;
        return NULL;
    }

    hb->rate = dfl_heartrate;
    return hb;
}

void heartbeat_set_flux (heartbeat_t *hb, flux_t *h)
{
    hb->h = h;
}

int heartbeat_set_rate (heartbeat_t *hb, double rate)
{
    if (rate < min_heartrate || rate > max_heartrate)
        goto error;
    hb->rate = rate;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

double heartbeat_get_rate (heartbeat_t *hb)
{
    return hb->rate;
}

static void timer_cb (flux_reactor_t *r, flux_watcher_t *w,
                      int revents, void *arg)
{
    heartbeat_t *hb = arg;
    flux_msg_t *msg = NULL;

    if (!(msg = flux_event_encode ("heartbeat.pulse", NULL))) {
        log_err ("heartbeat_encode");
        goto done;
    }
    if (flux_send (hb->h, msg, 0) < 0) {
        log_err ("flux_send");
        goto done;
    }
done:
    flux_msg_destroy (msg);
}

int heartbeat_start (heartbeat_t *hb)
{
    uint32_t rank;

    if (!hb->h) {
        errno = EINVAL;
        return -1;
    }
    if (flux_get_rank (hb->h, &rank) < 0)
        return -1;
    if (rank == 0) {
        flux_reactor_t *r = flux_get_reactor (hb->h);
        flux_reactor_now_update (r);
        if (!(hb->timer = flux_timer_watcher_create (r, hb->rate, hb->rate,
                                                     timer_cb, hb)))
            return -1;
        flux_watcher_start (hb->timer);
    }
    return 0;
}

void heartbeat_stop (heartbeat_t *hb)
{
    if (hb->timer)
        flux_watcher_stop (hb->timer);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
