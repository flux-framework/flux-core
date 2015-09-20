/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <errno.h>
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"

#include "heartbeat.h"

struct heartbeat_struct {
    flux_t h;
    double rate;
    flux_watcher_t *w;
    int send_epoch;
    int epoch;
    heartbeat_cb_f cb;
    void *cb_arg;
};

static const double min_heartrate = 0.01;   /* min seconds */
static const double max_heartrate = 30;     /* max seconds */
static const double dfl_heartrate = 2;


void heartbeat_destroy (heartbeat_t *hb)
{
    if (hb) {
        free (hb);
    }
}

heartbeat_t *heartbeat_create (void)
{
    heartbeat_t *hb = xzmalloc (sizeof (*hb));
    hb->rate = dfl_heartrate;
    return hb;
}

void heartbeat_set_flux (heartbeat_t *hb, flux_t h)
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

int heartbeat_set_ratestr (heartbeat_t *hb, const char *s)
{
    char *endptr;
    double rate = strtod (s, &endptr);
    if (rate == HUGE_VAL || endptr == optarg)
        goto error;
    if (!strcasecmp (endptr, "s") || *endptr == '\0')
        ;
    else if (!strcasecmp (endptr, "ms"))
        rate /= 1000.0;
    else
        goto error;
    return heartbeat_set_rate (hb, rate);
error:
    errno = EINVAL;
    return -1;
}

double heartbeat_get_rate (heartbeat_t *hb)
{
    return hb->rate;
}

int heartbeat_get_epoch (heartbeat_t *hb)
{
    return hb->epoch;
}

void heartbeat_set_callback (heartbeat_t *hb, heartbeat_cb_f cb, void *arg)
{
    hb->cb = cb;
    hb->cb_arg = arg;
}

static void heartbeat_cb (flux_reactor_t *r, flux_watcher_t *w,
                          int revents, void *arg)
{
    heartbeat_t *hb = arg;
    flux_msg_t *msg = NULL;

    if (!(msg = flux_heartbeat_encode (hb->send_epoch++))) {
        err ("heartbeat_encode");
        goto done;
    }
    if (flux_send (hb->h, msg, 0) < 0) {
        err ("flux_send");
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
    if (rank != 0) {
        errno = EINVAL;
        return -1;
    }
    if (!(hb->w = flux_timer_watcher_create (flux_get_reactor (hb->h),
                                             hb->rate, hb->rate,
                                             heartbeat_cb, hb)))
        return -1;
    flux_watcher_start (hb->w);
    return 0;
}

void heartbeat_stop (heartbeat_t *hb)
{
    if (hb->w)
        flux_watcher_stop (hb->w);
}

int heartbeat_recvmsg (heartbeat_t *hb, const flux_msg_t *msg)
{
    int rc = -1;
    int epoch;

    if (flux_heartbeat_decode (msg, &epoch) < 0)
        goto done;
    if (epoch < hb->epoch) { /* ensure epoch remains monotonic */
        errno = EPROTO;
        goto done;
    }
    hb->epoch = epoch;
    if (hb->cb)
        hb->cb (hb, hb->cb_arg);
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
