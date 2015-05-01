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
    zloop_t *zloop;
    double rate;
    int timer_id;
    int epoch;
    heartbeat_cb_f cb;
    void *cb_arg;
};

static const double min_heartrate = 0.01;   /* min seconds */
static const double max_heartrate = 30;     /* max seconds */
static const double dfl_heartrate = 2;


void heartbeat_destroy (heartbeat_t h)
{
    if (h) {
        free (h);
    }
}

heartbeat_t heartbeat_create (void)
{
    heartbeat_t h = xzmalloc (sizeof (*h));
    h->rate = dfl_heartrate;
    h->timer_id = -1;
    return h;
}

void heartbeat_set_loop (heartbeat_t h, zloop_t *zloop)
{
    h->zloop = zloop;
}

int heartbeat_set_rate (heartbeat_t h, double rate)
{
    if (rate < min_heartrate || rate > max_heartrate)
        goto error;
    h->rate = rate;
    return 0;
error:
    errno = EINVAL;
    return -1;
}

int heartbeat_set_ratestr (heartbeat_t h, const char *s)
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
    return heartbeat_set_rate (h, rate);
error:
    errno = EINVAL;
    return -1;
}

double heartbeat_get_rate (heartbeat_t h)
{
    return h->rate;
}

void heartbeat_set_epoch (heartbeat_t h, int epoch)
{
    h->epoch = epoch;
}

int heartbeat_get_epoch (heartbeat_t h)
{
    return h->epoch;
}

void heartbeat_next_epoch (heartbeat_t h)
{
    h->epoch++;
}

void heartbeat_set_cb (heartbeat_t h, heartbeat_cb_f cb, void *arg)
{
    h->cb = cb;
    h->cb_arg = arg;
}

static int heartbeat_cb (zloop_t *zl, int timer_id, void *arg)
{
    heartbeat_t h = arg;
    if (h->cb)
        h->cb (h, h->cb_arg);
    return 0;
}

int heartbeat_start (heartbeat_t h)
{
    unsigned long msec = h->rate * 1000;

    h->timer_id = zloop_timer (h->zloop, msec, 0, heartbeat_cb, h);

    return h->timer_id;
}

void heartbeat_stop (heartbeat_t h)
{
    if (h->timer_id != -1)
        zloop_timer_end (h->zloop, h->timer_id);
}

static JSON heartbeat_json_encode (heartbeat_t h)
{
    JSON out = Jnew ();
    Jadd_int (out, "epoch", h->epoch);
    return out;
}

zmsg_t *heartbeat_event_encode (heartbeat_t h)
{
    JSON out = heartbeat_json_encode (h);
    zmsg_t *zmsg = flux_msg_create (FLUX_MSGTYPE_EVENT);
    if (!zmsg)
        goto done;
    if (flux_msg_set_topic (zmsg, "hb") < 0) {
        zmsg_destroy (&zmsg);
        goto done;
    }
    if (flux_msg_set_payload_json (zmsg, out) < 0) {
        zmsg_destroy (&zmsg);
        goto done;
    }
done:
    Jput (out);
    return zmsg;
}

static int heartbeat_json_decode (heartbeat_t h, JSON out)
{
    int rc = -1;

    if (Jget_int (out, "epoch", &h->epoch) < 0) {
        errno = EPROTO;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int heartbeat_event_decode (heartbeat_t h, zmsg_t *zmsg)
{
    int rc = -1;
    int type;
    JSON out = NULL;

    if (flux_msg_get_type (zmsg, &type) < 0)
        goto done;
    if (type != FLUX_MSGTYPE_EVENT) {
        errno = EPROTO;
        goto done;
    }
    if (!flux_msg_streq_topic (zmsg, "hb")) {
        errno = EPROTO;
        goto done;
    }
    if (flux_msg_get_payload_json (zmsg, &out) < 0)
        goto done;
    if (out == NULL) {
        errno = EPROTO;
        goto done;
    }
    rc = heartbeat_json_decode (h, out);
done:
    if (out)
        Jput (out);
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
