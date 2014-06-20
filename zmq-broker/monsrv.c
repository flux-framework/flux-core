/* monsrv.c - monitoring plugin */

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

static const char *mon_conf_dir = "conf.mon.source";
const int red_timeout_msec = 2;

typedef struct {
    int epoch;
    flux_t h;
    bool master;
    int rank;
    char *rankstr;
    zhash_t *rcache;    /* hash of red_t by name */
} ctx_t;

static void mon_sink (flux_t h, void *item, int batchnum, void *arg);
static void mon_reduce (flux_t h, zlist_t *items, int batchnum, void *arg);

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->rcache);
    free (ctx->rankstr);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "monsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        ctx->master = flux_treeroot (h);
        ctx->rank = flux_rank (h);
        if (asprintf (&ctx->rankstr, "%d", ctx->rank) < 0)
            oom (); 
        if (!(ctx->rcache = zhash_new ()))
            oom ();
        flux_aux_set (h, "monsrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static red_t rcache_lookup (ctx_t *ctx, const char *name)
{
    return zhash_lookup (ctx->rcache, name);
}

static red_t rcache_add (ctx_t *ctx, const char *name)
{
    flux_t h = ctx->h;
    red_t r = flux_red_create (h, mon_sink, ctx);

    flux_red_set_reduce_fn (r, mon_reduce);
    if (ctx->master) {
        flux_red_set_flags (r, FLUX_RED_TIMEDFLUSH);
        flux_red_set_timeout_msec (r, red_timeout_msec);
    } else {
        flux_red_set_flags (r, FLUX_RED_HWMFLUSH);
    }
    zhash_insert (ctx->rcache, name, r);
    zhash_freefn (ctx->rcache, name, (zhash_free_fn *)flux_red_destroy);
    return r;
}

static int push_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    const char *name;
    red_t r;
    int epoch;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
            || !Jget_str (request, "name", &name)
            || !Jget_int (request, "epoch", &epoch)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!(r = rcache_lookup (ctx, name)))
        r = rcache_add (ctx, name);
    Jget (request);
    flux_red_append (r, request, epoch);
done:
    Jput (request);
    return rc;
}

static void poll_one (ctx_t *ctx, const char *name, const char *tag)
{
    red_t r;
    JSON res;

    if ((res = flux_rpc (ctx->h, NULL, "%s", tag))) {
        JSON data = Jnew ();
        JSON o = Jnew ();
        Jadd_int (o, "epoch", ctx->epoch);
        Jadd_str  (o, "name", name);
        Jadd_obj (o, "data", data);        
        Jadd_obj (data, ctx->rankstr, res);
        Jput (res);
        if (!(r = rcache_lookup (ctx, name)))
            r = rcache_add (ctx, name);
        flux_red_append (r, o, ctx->epoch);
    }
}

static void poll_all (ctx_t *ctx)
{
    kvsdir_t dir = NULL;
    kvsitr_t itr = NULL;
    const char *name, *tag;

    if (kvs_get_dir (ctx->h, &dir, "%s", mon_conf_dir) < 0)
        goto done;
    if (!(itr = kvsitr_create (dir)))
        oom ();
    while ((name = kvsitr_next (itr))) {
        JSON ent = NULL;
        if (kvsdir_get (dir, name, &ent) == 0 && Jget_str (ent, "tag", &tag)) {
            poll_one (ctx, name, tag);
        }
        Jput (ent);
    }
done:
    if (itr)
        kvsitr_destroy (itr);
    if (dir)
        kvsdir_destroy (dir);
}

static int hb_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON event = NULL;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
                || !Jget_int  (event, "epoch", &ctx->epoch)) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    poll_all (ctx);
done:
    Jput (event);
    return rc;
}

/* Detect the presence (or absence) of content in our conf KVS space.
 * We will ignore hb events to reduce overhead if there is no content.
 */
static void conf_cb (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    kvsitr_t itr;
    int entries = 0;

    if (errnum == 0) {
        if (!(itr = kvsitr_create (dir)))
            oom ();
        while (kvsitr_next (itr))
            entries++;
        kvsitr_destroy (itr);
    }
    if (entries > 0) {
        if (flux_event_subscribe (ctx->h, "hb") < 0) {
            flux_log (ctx->h, LOG_ERR, "flux_event_subscribe: %s",
                      strerror (errno));
        }
    } else {
        if (flux_event_unsubscribe (ctx->h, "hb") < 0) {
            flux_log (ctx->h, LOG_ERR, "flux_event_subscribe: %s",
                      strerror (errno));
        }
    }
}

static void mon_sink (flux_t h, void *item, int batchnum, void *arg)
{
    ctx_t *ctx = arg;
    JSON o = item;
    JSON oo, data, odata;
    const char *name;
    int epoch;
    char *key;

    if (!Jget_str (o, "name", &name) || !Jget_int (o, "epoch", &epoch)
                                     || !Jget_obj (o, "data", &data))
        return;
    if (ctx->master) {  /* sink to the kvs */
        if (asprintf (&key, "mon.%s.%d", name, epoch) < 0)
            oom ();
        if (kvs_get (h, key, &oo) == 0) {
            if (Jget_obj (oo, "data", &odata))
                Jmerge (data, odata);
            Jput (oo);
        }
        kvs_put (h, key, o);
        kvs_commit (h);
        free (key);
    } else {            /* push upstream */
        flux_request_send (h, o, "%s", "mon.push");
    }
    Jput (o);
}

static void mon_reduce (flux_t h, zlist_t *items, int batchnum, void *arg)
{
    zlist_t *tmp; 
    int e1, e2;
    JSON o1, o2, d1, d2;

    if (!(tmp = zlist_new ()))
        oom ();
    while ((o1 = zlist_pop (items))) {
        if (zlist_append (tmp, o1) < 0)
            oom ();
    }
    while ((o1 = zlist_pop (tmp))) {
        if (!Jget_int (o1, "epoch", &e1) || !Jget_obj (o1, "data", &d1)) {
            Jput (o1);
            continue;
        }
        o2 = zlist_first (items);
        while (o2) {
            if (Jget_int (o2, "epoch", &e2) && e1 == e2)
                break;
            o2 = zlist_next (items);
        }
        if (o2) {
            if (Jget_obj (o2, "data", &d2))
                Jmerge (d2, d1);
            Jput (o1);
        } else {
            if (zlist_append (items, o1) < 0)
                oom ();
        }
    }
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "hb",             hb_cb },
    { FLUX_MSGTYPE_REQUEST, "mon.push",       push_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_dir (h, conf_cb, ctx, mon_conf_dir) < 0) {
        flux_log (ctx->h, LOG_ERR, "kvs_watch_dir: %s", strerror (errno));
        return -1;
    }
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_add: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("mon");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
