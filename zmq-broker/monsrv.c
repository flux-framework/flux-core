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

static const char *mon_conf_dir = "conf.mon.source";

typedef struct {
    int epoch;
    flux_t h;
    bool master;
    int rank;
    char *rankstr;
    zhash_t *cache; /* cached monitoring data, key=name */
} ctx_t;

static void freectx (ctx_t *ctx)
{
    free (ctx->rankstr);
    zhash_destroy (&ctx->cache);
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
        if (!(ctx->cache = zhash_new ()))
            oom ();
        flux_aux_set (h, "monsrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static JSON cache_add (ctx_t *ctx, const char *name, JSON *entp)
{
    JSON ent = NULL;
    int old_epoch, new_epoch;

    if (!Jget_int (*entp, "epoch", &new_epoch))
        goto done;
    if (!(ent = zhash_lookup (ctx->cache, name))
            || !Jget_int (ent, "epoch", &old_epoch) || old_epoch < new_epoch) {
        ent = *entp;
        *entp = NULL;
        zhash_update (ctx->cache, name, ent);
        zhash_freefn (ctx->cache, name, (zhash_free_fn *)Jput);
    } else if (new_epoch < old_epoch)
        ent = NULL;
done:
    return ent;
}

static void poll_one (ctx_t *ctx, const char *name, JSON *entp)
{
    JSON ent = cache_add (ctx, name, entp);
    JSON res, data;
    const char *tag;

    if (ent && Jget_str (ent, "tag", &tag)) {
        if ((res = flux_rpc (ctx->h, NULL, "%s", tag))) {
            if (Jget_obj (ent, "data", &data))
                Jadd_obj (data, ctx->rankstr, res);
            Jput (res);
        }
    }
}

static void poll_all (ctx_t *ctx)
{
    kvsdir_t dir = NULL;
    kvsitr_t itr = NULL;
    const char *name;

    if (kvs_get_dir (ctx->h, &dir, "%s", mon_conf_dir) < 0)
        goto done;
    if (!(itr = kvsitr_create (dir)))
        oom ();
    while ((name = kvsitr_next (itr))) {
        JSON ent = NULL;
        if (kvsdir_get (dir, name, &ent) == 0) {
            Jadd_int (ent, "epoch", ctx->epoch);
            Jadd_new (ent, "data");
            poll_one (ctx, name, &ent);
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

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "hb",             hb_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int monsrv_main (flux_t h, zhash_t *args)
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

const struct plugin_ops ops = {
    .main    = monsrv_main,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
