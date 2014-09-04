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

/* reduce.c - reduction pattern for cmb */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "util.h"
#include "log.h"
#include "plugin.h"
#include "shortjson.h"

#include "reduce.h"

typedef struct hwm_struct *hwm_t;

struct red_struct {
    FluxSinkFn  sinkfn;
    FluxRedFn   redfn;
    void *arg;
    zlist_t *items;
    flux_t h;
    int flags;
    int timeout_msec;
    int timer_id;
    bool timer_armed;

    int last_hwm;
    int cur_hwm;
    int cur_batchnum;
};

static void timer_enable (red_t r);
static void timer_disable (red_t r);

static bool hwm_flushable (red_t r);
static bool hwm_valid (red_t r);

red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, void *arg)
{
    red_t r = xzmalloc (sizeof (*r));
    r->arg = arg;
    r->h = h;
    assert (sinkfn != NULL); /* FIXME */
    r->sinkfn = sinkfn;
    if (!(r->items = zlist_new ()))
        oom ();
    return r;
}

void flux_red_destroy (red_t r)
{
    flux_red_flush (r);
    zlist_destroy (&r->items);
    free (r);
}

void flux_red_set_timeout_msec (red_t r, int msec)
{
    r->timeout_msec = msec;
}

void flux_red_set_reduce_fn (red_t r, FluxRedFn redfn)
{
    r->redfn = redfn;
}

void flux_red_set_flags (red_t r, int flags)
{
    r->flags = flags;
}

void flux_red_flush (red_t r)
{
    void *item;

    while ((item = zlist_pop (r->items)))
        r->sinkfn (r->h, item, r->cur_batchnum, r->arg);
    timer_disable (r); /* no-op if not armed */
}

static int append_late_item (red_t r, void *item, int batchnum)
{
    zlist_t *items;
    void *i;

    if (!(items = zlist_new()))
        oom ();
    if (zlist_append (items, item) < 0)
        oom ();
    if (r->redfn)
        r->redfn (r->h, items, batchnum, r->arg);
    while ((i = zlist_pop (items)))
        r->sinkfn (r->h, i, batchnum, r->arg);
    zlist_destroy (&items);
    return 0;
}

int flux_red_append (red_t r, void *item, int batchnum)
{
    int rc = 0;

    if (batchnum < r->cur_batchnum) {
        if (batchnum == r->cur_batchnum - 1)
            r->last_hwm++;
        return append_late_item (r, item, batchnum);
    }
    if (batchnum > r->cur_batchnum) {
        flux_red_flush (r);
        r->last_hwm = r->cur_hwm;
        r->cur_hwm = 1;
        r->cur_batchnum = batchnum;
    } else
        r->cur_hwm++;
    assert (batchnum == r->cur_batchnum);
    if (zlist_append (r->items, item) < 0)
        oom ();
    if (r->redfn)
        r->redfn (r->h, r->items, r->cur_batchnum, r->arg);

    if ((r->flags & FLUX_RED_HWMFLUSH)) {
        if (!hwm_valid (r) || hwm_flushable (r))
            flux_red_flush (r);
    }
    if ((r->flags & FLUX_RED_TIMEDFLUSH)) {
        if (zlist_size (r->items) > 0)
            timer_enable (r);
    }
    if (r->flags == 0)
        flux_red_flush (r);
    return rc;
}

static int timer_cb (flux_t h, void *arg)
{
    red_t r = arg;
    int rc = 0;

    r->timer_armed = false; /* it's a one-shot timer */
    flux_red_flush (r);
    return rc;
}

static void timer_enable (red_t r)
{
    if (!r->timer_armed) {
        r->timer_id = flux_tmouthandler_add (r->h, r->timeout_msec, true,
                                             timer_cb, r);
        r->timer_armed = true;
    }
}

static void timer_disable (red_t r)
{
    if (r->timer_armed) {
        flux_tmouthandler_remove (r->h, r->timer_id);
        r->timer_armed = false;
    }
}

static bool hwm_flushable (red_t r)
{
    return (r->last_hwm > 0 && r->last_hwm == r->cur_hwm);
}

static bool hwm_valid (red_t r)
{
    return (r->last_hwm > 0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
