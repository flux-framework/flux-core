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
};

static int red_timeout (flux_t h, void *arg)
{
    red_t r = arg;
    int rc = 0;

    r->armed = false; /* it's a one-shot timer */
    flux_red_flush (r);
    return rc;
}

red_t flux_red_create (flux_t h, FluxSinkFn sinkfn, FluxRedFn redfn,
                       int flags, void *arg)
{
    red_t r = xzmalloc (sizeof (*r));
    r->sinkfn = sinkfn;
    r->redfn = redfn;
    r->arg = arg;
    r->flags = flags;
    r->h = h;
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

void flux_red_flush (red_t r)
{
    void *item;

    while ((item = zlist_pop (r->items))) {
        if (r->sinkfn)
            r->sinkfn (r->h, item, r->arg);
    }
    if ((r->flags & FLUX_RED_TIMEDFLUSH)) {
        if (r->armed) {
            flux_tmouthandler_remove (r->h, r->timer_id);
            r->armed = false;
        }
    }
}

int flux_red_append (red_t r, void *item)
{
    int count;

    if (zlist_append (r->items, item) < 0)
        oom ();
    if (r->redfn)
        r->redfn (r->h, r->items, r->arg);
    if (r->flags & FLUX_RED_AUTOFLUSH)
        flux_red_flush (r);
    count = zlist_size (r->items);
    if ((r->flags & FLUX_RED_TIMEDFLUSH)) {
        if (count > 0 && !r->armed)  {
            r->timer_id = flux_tmouthandler_add (r->h, r->timeout, true,
                                                 red_timeout, r);
            r->armed = true;
        }
    }

    return count;
}

void flux_red_set_timeout_msec (red_t r, int msec)
{
    r->timeout = msec;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
