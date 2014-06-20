/* reduce.c - reduction pattern for cmb */

#define _GNU_SOURCE
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
    int timeout; /* in msec */
    int timer_id;
    bool armed;
    hwm_t hwm;
};

static void timer_enable (red_t r);
static void timer_disable (red_t r);

static void hwm_destroy (hwm_t h);
static hwm_t hwm_create (void);
static void hwm_acct (hwm_t h, int count, int batchnum);
static bool hwm_flushable (hwm_t h);
static bool hwm_valid (hwm_t h);

red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, void *arg)
{
    red_t r = xzmalloc (sizeof (*r));
    r->arg = arg;
    r->h = h;
    r->sinkfn = sinkfn;
    if (!(r->items = zlist_new ()))
        oom ();
    if ((r->flags & FLUX_RED_HWMFLUSH))
        r->hwm = hwm_create ();

    return r;
}

void flux_red_destroy (red_t r)
{
    flux_red_flush (r);
    if (r->hwm)
        hwm_destroy (r->hwm);
    zlist_destroy (&r->items);
    free (r);
}

void flux_red_set_timeout_msec (red_t r, int msec)
{
    r->timeout = msec;
}

void flux_red_set_reduce_fn (red_t r, FluxRedFn redfn)
{
    r->redfn = redfn;
}

void flux_red_set_flags (red_t r, int flags)
{
    r->flags = flags;
    if ((r->flags & FLUX_RED_HWMFLUSH)) {
        if (r->hwm == NULL)
            r->hwm = hwm_create ();
    } else {
        if (r->hwm != NULL) {
            hwm_destroy (r->hwm);
            r->hwm = NULL;
        }
    }
}

void flux_red_flush (red_t r)
{
    void *item;

    while ((item = zlist_pop (r->items))) {
        if (r->sinkfn)
            r->sinkfn (r->h, item, r->arg);
        else
            ; /* presumably we don't own the items - otherwise mem leak! */
    }
    timer_disable (r);
}

int flux_red_append (red_t r, void *item, int batchnum)
{
    int count;

    if (zlist_append (r->items, item) < 0)
        oom ();
    if (r->redfn)
        r->redfn (r->h, r->items, r->arg);
    if ((r->flags & FLUX_RED_HWMFLUSH)) {
        hwm_acct (r->hwm, 1, batchnum);
        if (!hwm_valid (r->hwm) || hwm_flushable (r->hwm))
            flux_red_flush (r);
    }
    count = zlist_size (r->items);
    if ((r->flags & FLUX_RED_TIMEDFLUSH) && count > 0)
        timer_enable (r);

    return count;
}

static int timer_cb (flux_t h, void *arg)
{
    red_t r = arg;
    int rc = 0;

    r->armed = false; /* it's a one-shot timer */
    flux_red_flush (r);
    return rc;
}

static void timer_enable (red_t r)
{
    if (!r->armed) {
        r->timer_id = flux_tmouthandler_add (r->h, r->timeout, true,
                                             timer_cb, r);
        r->armed = true;
    }
}

static void timer_disable (red_t r)
{
    if (r->armed) {
        flux_tmouthandler_remove (r->h, r->timer_id);
        r->armed = false;
    }
}

struct hwm_struct {
    int last;
    int cur;
    int cur_batchnum;
};

static bool hwm_flushable (hwm_t h)
{
    return (h->last > 0 && h->last == h->cur);
}

static bool hwm_valid (hwm_t h)
{
    return (h->last > 0);
}

static hwm_t hwm_create (void)
{
    hwm_t h = xzmalloc (sizeof *h);
    return h;
}

static void hwm_destroy (hwm_t h)
{
    free (h);
}

static void hwm_acct (hwm_t h, int count, int batchnum)
{
    if (batchnum == h->cur_batchnum - 1)
        h->last++;
    else if (batchnum == h->cur_batchnum)
        h->cur++;
    else if (batchnum > h->cur_batchnum) {
        h->last = h->cur;
        h->cur = 1;
        h->cur_batchnum = batchnum;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
