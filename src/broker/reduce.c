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
#include <stdbool.h>
#include <czmq.h>
#include <flux/core.h>

#include "reduce.h"

struct flux_reduce_struct {
    struct flux_reduce_ops ops;
    void *arg;

    zlist_t *items; /* set of current items */
    void *old_item; /* flux_reduce_pop() pops old_item if old_flag is true */
    bool old_flag;

    uint32_t rank;
    flux_t *h;
    flux_reactor_t *reactor;
    int flags;

    flux_watcher_t *timer;
    double timeout;
    bool timer_armed;

    unsigned int hwm;
    bool hwm_readonly;
    unsigned int count; /* count of items in current batch towards hwm */

    int batchnum;
    bool flushed;
};

static void flush_current (flux_reduce_t *r);

static void timer_cb (flux_reactor_t *reactor,
                      flux_watcher_t *w,
                      int revents,
                      void *arg)
{
    flux_reduce_t *r = arg;
    flush_current (r);
}

flux_reduce_t *flux_reduce_create (flux_t *h,
                                   struct flux_reduce_ops ops,
                                   double timeout,
                                   void *arg,
                                   int flags)
{
    if (!h || ((flags & FLUX_REDUCE_HWMFLUSH) && !ops.itemweight)
        || ((flags & FLUX_REDUCE_TIMEDFLUSH) && timeout <= 0)) {
        errno = EINVAL;
        return NULL;
    }
    flux_reduce_t *r = calloc (1, sizeof (*r));
    if (!r)
        return NULL;
    r->h = h;
    if (!(r->reactor = flux_get_reactor (h))) {
        flux_reduce_destroy (r);
        return NULL;
    }
    r->ops = ops;
    r->rank = 0;
    if (flux_get_rank (h, &r->rank) < 0) {
        flux_reduce_destroy (r);
        return NULL;
    }

    r->arg = arg;
    r->flags = flags;
    r->timeout = timeout;
    if (!(r->items = zlist_new ())) {
        flux_reduce_destroy (r);
        return NULL;
    }
    if ((flags & FLUX_REDUCE_TIMEDFLUSH)) {
        if (!(r->timer = flux_timer_watcher_create (r->reactor, 0., 0., timer_cb, r))) {
            flux_reduce_destroy (r);
            return NULL;
        }
    }
    return r;
}

void flux_reduce_destroy (flux_reduce_t *r)
{
    if (r) {
        int saved_errno = errno;
        if (r->items) {
            void *item;
            while ((item = zlist_pop (r->items))) {
                if (r->ops.destroy)
                    r->ops.destroy (item);
            }
            zlist_destroy (&r->items);
        }
        if (r->ops.destroy && r->old_item)
            r->ops.destroy (r->old_item);
        if (r->timer) {
            flux_watcher_stop (r->timer);
            flux_watcher_destroy (r->timer);
        }
        free (r);
        errno = saved_errno;
    }
}

/* Empty the queue of items.
 */
static void flush_current (flux_reduce_t *r)
{
    void *item;

    if (zlist_size (r->items) > 0) {
        if (r->rank > 0) {
            if (r->ops.forward)
                r->ops.forward (r, r->batchnum, r->arg);
        } else {
            if (r->ops.sink)
                r->ops.sink (r, r->batchnum, r->arg);
        }
        while ((item = zlist_pop (r->items))) {
            if (r->ops.destroy)
                r->ops.destroy (item);
        }
    }
    if (r->timer) {
        flux_watcher_stop (r->timer);
        r->timer_armed = false;
    }
    r->flushed = true;
}

/* Flush one item that is a straggler from a previous batch.
 */
static void flush_old (flux_reduce_t *r, void *item, int batchnum)
{
    assert (r->old_item == NULL);
    r->old_item = item;
    r->old_flag = true;

    if (r->rank > 0) {
        if (r->ops.forward)
            r->ops.forward (r, batchnum, r->arg);
    } else {
        if (r->ops.sink)
            r->ops.sink (r, batchnum, r->arg);
    }
    if (r->ops.destroy)
        r->ops.destroy (r->old_item);
    r->old_item = NULL;
    r->old_flag = false;
}

int flux_reduce_append (flux_reduce_t *r, void *item, int batchnum)
{
    int rc = -1;
    int count = 1;

    if (r->ops.itemweight)
        count = r->ops.itemweight (item);

    if (batchnum < r->batchnum - 1) {
        flush_old (r, item, batchnum);
    } else if (batchnum == r->batchnum - 1) {
        if (!r->hwm_readonly)
            r->hwm += count;
        flush_old (r, item, batchnum);
    } else if (batchnum == r->batchnum && r->flushed) {
        r->count += count;
        flush_old (r, item, batchnum);
    } else {
        if (batchnum > r->batchnum) {
            flush_current (r);
            if (!r->hwm_readonly)
                r->hwm = r->count;
            r->count = 0;
            r->batchnum = batchnum;
            r->flushed = false;
        }

        assert (batchnum == r->batchnum);
        r->count += count;
        if (zlist_push (r->items, item) < 0)
            goto done;
        if (r->ops.reduce && zlist_size (r->items) > 1)
            r->ops.reduce (r, r->batchnum, r->arg);

        if ((r->flags & FLUX_REDUCE_HWMFLUSH)) {
            if (r->count >= r->hwm)
                flush_current (r);
        }
        if ((r->flags & FLUX_REDUCE_TIMEDFLUSH)) {
            if (zlist_size (r->items) > 0 && !r->timer_armed) {
                flux_timer_watcher_reset (r->timer, r->timeout, 0);
                flux_watcher_start (r->timer);
                r->timer_armed = true;
            }
        }
        if (!(r->flags & FLUX_REDUCE_HWMFLUSH)
            && !(r->flags & FLUX_REDUCE_TIMEDFLUSH)) {
            flush_current (r);
        }
    }
    rc = 0;
done:
    return rc;
}

void *flux_reduce_pop (flux_reduce_t *r)
{
    void *item = NULL;
    if (r->old_flag) {
        item = r->old_item;
        r->old_item = NULL;
    } else
        item = zlist_pop (r->items);
    return item;
}

int flux_reduce_push (flux_reduce_t *r, void *item)
{
    return zlist_push (r->items, item);
}

int flux_reduce_opt_get (flux_reduce_t *r, int option, void *val, size_t size)
{
    switch (option) {
        case FLUX_REDUCE_OPT_TIMEOUT:
            if (size != sizeof (r->timeout))
                goto invalid;
            memcpy (val, &r->timeout, size);
            break;
        case FLUX_REDUCE_OPT_HWM:
            if (size != sizeof (r->count))
                goto invalid;
            memcpy (val, &r->hwm, size);
            break;
        case FLUX_REDUCE_OPT_COUNT: {
            unsigned int count = zlist_size (r->items);
            if (size != sizeof (count))
                goto invalid;
            memcpy (val, &count, size);
            break;
        }
        case FLUX_REDUCE_OPT_WCOUNT: {
            unsigned int count = 0;
            void *item = zlist_first (r->items);
            while (item) {
                count += r->ops.itemweight ? r->ops.itemweight (item) : 1;
                item = zlist_next (r->items);
            }
            if (size != sizeof (count))
                goto invalid;
            memcpy (val, &count, size);
            break;
        }
        default:
            goto invalid;
    }
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

int flux_reduce_opt_set (flux_reduce_t *r, int option, void *val, size_t size)
{
    switch (option) {
        case FLUX_REDUCE_OPT_TIMEOUT:
            if (size != sizeof (r->timeout))
                goto invalid;
            memcpy (&r->timeout, val, size);
            break;
        case FLUX_REDUCE_OPT_HWM:
            if (size != sizeof (r->hwm))
                goto invalid;
            memcpy (&r->hwm, val, size);
            r->hwm_readonly = true;
            break;
        default:
            goto invalid;
    }
    return 0;
invalid:
    errno = EINVAL;
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
