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
#include "reduce.h"
#include "reactor.h"

#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"


typedef struct hwm_struct *hwm_t;

struct flux_red_struct {
    flux_sink_f sinkfn;
    flux_red_f redfn;
    void *arg;
    flux_redstack_t items;
    flux_t h;
    int flags;
    int timeout_msec;
    flux_timer_watcher_t *timer;

    int last_hwm;
    int cur_hwm;
    int cur_batchnum;
};

struct flux_redstack_struct {
    zlist_t *l;
};

static void timer_cb (flux_t h, flux_timer_watcher_t *w, int revents, void *arg);

static bool hwm_flushable (flux_red_t r);
static bool hwm_valid (flux_red_t r);

static flux_redstack_t redstack_create (void)
{
    flux_redstack_t s = xzmalloc (sizeof (*s));
    if (!(s->l = zlist_new ()))
        oom();
    return s;
}

static void redstack_destroy (flux_redstack_t s)
{
    if (s) {
        zlist_destroy (&s->l);
        free (s);
    }
}

void *flux_redstack_pop (flux_redstack_t s)
{
    return zlist_pop (s->l);
}

void flux_redstack_push (flux_redstack_t s, void *item)
{
    if (zlist_push (s->l, item) < 0)
        oom ();
}

int flux_redstack_count (flux_redstack_t s)
{
    return zlist_size (s->l);
}

flux_red_t flux_red_create (flux_t h, flux_sink_f sinkfn, void *arg)
{
    flux_red_t r = xzmalloc (sizeof (*r));
    r->arg = arg;
    r->h = h;
    assert (sinkfn != NULL); /* FIXME */
    r->sinkfn = sinkfn;
    r->items = redstack_create ();
    r->timer = flux_timer_watcher_create (1E-3*r->timeout_msec, 0, timer_cb, r);
    if (!r->timer)
        oom ();
    return r;
}

void flux_red_destroy (flux_red_t r)
{
    flux_red_flush (r);
    redstack_destroy (r->items);
    if (r->timer) {
        flux_timer_watcher_stop (r->h, r->timer);
        flux_timer_watcher_destroy (r->timer);
    }
    free (r);
}

void flux_red_set_timeout_msec (flux_red_t r, int msec)
{
    r->timeout_msec = msec;
}

void flux_red_set_reduce_fn (flux_red_t r, flux_red_f redfn)
{
    r->redfn = redfn;
}

void flux_red_set_flags (flux_red_t r, int flags)
{
    r->flags = flags;
}

void flux_red_flush (flux_red_t r)
{
    void *item;

    while ((item = flux_redstack_pop (r->items)))
        r->sinkfn (r->h, item, r->cur_batchnum, r->arg);
    flux_timer_watcher_stop (r->h, r->timer);
}

static int append_late_item (flux_red_t r, void *item, int batchnum)
{
    flux_redstack_t items = redstack_create ();
    flux_redstack_push (items, item);
    if (r->redfn)
        r->redfn (r->h, items, batchnum, r->arg);
    while ((item = flux_redstack_pop (items)))
        r->sinkfn (r->h, item, batchnum, r->arg);
    redstack_destroy (items);
    return 0;
}

int flux_red_append (flux_red_t r, void *item, int batchnum)
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
    flux_redstack_push (r->items, item);
    if (r->redfn)
        r->redfn (r->h, r->items, r->cur_batchnum, r->arg);

    if ((r->flags & FLUX_RED_HWMFLUSH)) {
        if (!hwm_valid (r) || hwm_flushable (r))
            flux_red_flush (r);
    }
    if ((r->flags & FLUX_RED_TIMEDFLUSH)) {
        if (flux_redstack_count (r->items) > 0)
            flux_timer_watcher_start (r->h, r->timer);
    }
    if (r->flags == 0)
        flux_red_flush (r);
    return rc;
}

static void timer_cb (flux_t h, flux_timer_watcher_t *w, int revents, void *arg)
{
    flux_red_t r = arg;
    flux_red_flush (r);
}

static bool hwm_flushable (flux_red_t r)
{
    return (r->last_hwm > 0 && r->last_hwm == r->cur_hwm);
}

static bool hwm_valid (flux_red_t r)
{
    return (r->last_hwm > 0);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
