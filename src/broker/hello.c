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
#include <flux/core.h>

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"
#include "src/common/libutil/monotime.h"

#include "endpt.h"
#include "heartbeat.h"
#include "overlay.h"
#include "hello.h"

struct hello_struct {
    nodeset_t nodeset;
    uint32_t size;
    uint32_t count;
    double timeout;
    hello_cb_f cb;
    void *cb_arg;
    overlay_t ov;
    zloop_t *zl;
    int tid;
    struct timespec start;
};

static int timer_cb (zloop_t *zl, int tid, void *arg);


hello_t hello_create (void)
{
    hello_t h = xzmalloc (sizeof (*h));
    h->tid = -1;
    return h;
}

void hello_destroy (hello_t h)
{
    if (h) {
        if (h->nodeset)
            nodeset_destroy (h->nodeset);
        free (h);
    }
}

void hello_set_size (hello_t h, uint32_t size)
{
    h->size = size;
}

uint32_t hello_get_size (hello_t h)
{
    return h->size;
}

void hello_set_overlay (hello_t h, overlay_t ov)
{
    h->ov = ov;
}

void hello_set_zloop (hello_t h, zloop_t *zl)
{
    h->zl = zl;
}

void hello_set_timeout (hello_t h, double seconds)
{
    h->timeout = seconds;
}

double hello_get_time (hello_t h)
{
    if (!monotime_isset (h->start))
        return 0;
    return monotime_since (h->start) / 1000;
}

void hello_set_cb (hello_t h, hello_cb_f cb, void *arg)
{
    h->cb = cb;
    h->cb_arg = arg;
}

uint32_t hello_get_count (hello_t h)
{
    return h->count;
}

const char *hello_get_nodeset (hello_t h)
{
    if (!h->nodeset)
        return NULL;
    return nodeset_str (h->nodeset);
}

static int hello_add_rank (hello_t h, uint32_t rank)
{
    if (!h->nodeset)
        h->nodeset = nodeset_new_size (h->size);
    if (!nodeset_add_rank (h->nodeset, rank)) {
        errno = EPROTO;
        return -1;
    }
    h->count++;
    if (h->count == h->size)
        (void)timer_cb (h->zl, h->tid, h);
    return 0;
}

int hello_recv (hello_t h, zmsg_t **zmsg)
{
    char *sender = NULL;
    int rc = -1;
    uint32_t rank;

    if (flux_msg_get_route_first (*zmsg, &sender) < 0)
        goto done;
    rank = strtoul (sender, NULL, 10);
    if (hello_add_rank (h, rank) < 0)
        goto done;
    zmsg_destroy (zmsg);
    rc = 0;
done:
    if (sender)
        free (sender);
    return rc;
}

static int hello_send (hello_t h)
{
    zmsg_t *zmsg = NULL;
    int rc = -1;

    if (!(zmsg = flux_msg_create (FLUX_MSGTYPE_REQUEST)))
        goto done;
    if (flux_msg_set_topic (zmsg, "cmb.hello") < 0)
        goto done;
    if (flux_msg_set_nodeid (zmsg, 0, 0) < 0)
        goto done;
    if (flux_msg_enable_route (zmsg) < 0)
        goto done;
    rc = overlay_sendmsg_parent (h->ov, &zmsg);
done:
    zmsg_destroy (&zmsg);
    return rc;
}

static int timer_cb (zloop_t *zl, int tid, void *arg)
{
    hello_t h = arg;
    if (h->cb) {
        h->cb (h, h->cb_arg);
        if (h->timeout == 0.0 || h->count == h->size) {
            if (h->tid != -1) {
                zloop_timer_end (h->zl, h->tid);
                h->tid = -1;
            }
        }
    }
    return 0;
}

int hello_start (hello_t h, uint32_t rank)
{
    int rc = -1;

    if (rank == 0) {
        if (h->timeout > 0) {
            h->tid = zloop_timer (h->zl, h->timeout * 1000, 0, timer_cb, h);
            if (h->tid < 0)
                goto done;
        }
        monotime (&h->start);
        if (hello_add_rank (h, rank) < 0)
            goto done;
    } else {
        if (hello_send (h) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
