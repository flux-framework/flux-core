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

typedef enum {
    SRC_RPC,
} source_type_t;
typedef struct {
    source_type_t type;
    union {
        struct {
            char *tag;
        } rpc;
    };
} source_t;

typedef enum {
    CTYPE_ALWAYS,       /* commit on every heartbeat */
    CTYPE_ONREQUEST,    /* fence when mon.commit event received */
    CTYPE_ONDEL,        /* commit when source list is modified */
} ctype_t;
typedef struct {
    int epoch;
    zhash_t *sources;
    flux_t h;
    bool master;
    ctype_t ctype;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->sources);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "monsrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->h = h;
        if (!(ctx->sources = zhash_new ()))
            oom ();
        ctx->master = flux_treeroot (h);
        ctx->ctype = CTYPE_ALWAYS;
        flux_aux_set (h, "kvssrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static void source_destroy (source_t *src)
{
    switch (src->type) {
        case SRC_RPC:
            free (src->rpc.tag);
    }
    free (src);
}

static source_t *source_create_rpc (const char *tag)
{
    source_t *src = xzmalloc (sizeof (*src));
    src->type = SRC_RPC;
    src->rpc.tag = xstrdup (tag);
    return src;
}

static json_object *mon_source (ctx_t *ctx)
{
    json_object *o, *data = NULL;
    zlist_t *keys;
    char *key;
    source_t *src;

    if (zhash_size (ctx->sources) > 0) {
        data = util_json_object_new_object ();;
        if (!(keys = zhash_keys (ctx->sources)))
            oom ();
        while ((key = zlist_pop (keys))) {
            src = zhash_lookup (ctx->sources, key);
            if (src) {
                switch (src->type) {
                    case SRC_RPC:
                        if ((o = flux_rpc (ctx->h, NULL, "%s", src->rpc.tag)))
                            json_object_object_add (data, key, o);
                        break;
                }
            }
            free (key);
        }
        zlist_destroy (&keys);
    }
    return data;
}

static int heartbeat_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *event = NULL;
    int rc = 0;
    json_object *o = NULL;
    char *key = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
                || util_json_object_get_int (event, "epoch", &ctx->epoch) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if ((o = mon_source (ctx))) {
        if (asprintf (&key, "mon.%d.%d", flux_rank (h), ctx->epoch) < 0)
            oom ();
        if (kvs_put (h, key, o) < 0) {
            flux_log (h, LOG_ERR, "%s: kvs_put %s: %s", __FUNCTION__, key,
                      strerror (errno));
            goto done;
        }
        if (ctx->ctype == CTYPE_ALWAYS) {
            if (kvs_commit (h) < 0) {
                flux_log (h, LOG_ERR, "%s: kvs_commit %s: %s", __FUNCTION__,
                          key, strerror (errno));
                goto done;
            }
        }
    }
done:
    if (o)
        json_object_put (o);
    if (key)
        free (key);
    if (event)
        json_object_put (event);
    return rc;
}

static int commit_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    //ctx_t *ctx = arg;
    json_object *event = NULL;
    int nprocs;
    const char *name;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
                || util_json_object_get_string (event, "name", &name) < 0
                || util_json_object_get_int (event, "nprocs", &nprocs) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (kvs_fence (h, name, nprocs) < 0) {
        flux_log (h, LOG_ERR, "%s: kvs_fence: %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
done:
    if (event)
        json_object_put (event);
    return rc;
}

static void set_source (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    const char *key;
    kvsitr_t itr;
    json_object *o;

    if (errnum > 0)
        return;

    /* FIXME: drop and recreate isn't very efficient and won't work
     * if we start keeping state in the source_t.
     */
    zhash_destroy (&ctx->sources);
    if (!(ctx->sources = zhash_new ()))
        oom ();
    if (!(itr = kvsitr_create (dir)))
        oom ();
    while ((key = kvsitr_next (itr))) {
        const char *type;
        const char *tag;
        source_t *src;

        if (kvsdir_get (dir, key, &o) < 0)
            continue;
        if (util_json_object_get_string (o, "type", &type) == 0
                && !strcmp (type, "rpc")
                && util_json_object_get_string (o, "tag", &tag) == 0) {
            src = source_create_rpc (tag);
            zhash_insert (ctx->sources, key, src);
            zhash_freefn (ctx->sources, key, (zhash_free_fn *)source_destroy);
        }
        json_object_put (o);
    }
    kvsitr_destroy (itr);

    if (zhash_size (ctx->sources) > 0) {
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

    /* "Commit on delete" is really "commit on source list change".
     */
    if (ctx->ctype == CTYPE_ONDEL) {
        if (kvs_commit (ctx->h) < 0) {
            flux_log (ctx->h, LOG_ERR, "%s: kvs_commit %s: %s", __FUNCTION__,
                      key, strerror (errno));
        }
    }
}

static void set_commit (const char *path, const char *s, void *arg, int errnum)
{
    ctx_t *ctx = arg;

    if (errnum > 0 || !strcmp (s, "always"))
        ctx->ctype = CTYPE_ALWAYS;
    else if (!strcmp (s, "ondel"))
        ctx->ctype = CTYPE_ONDEL;
    else if (!strcmp (s, "onrequest"))
        ctx->ctype = CTYPE_ONREQUEST;
    else
        ctx->ctype = CTYPE_ALWAYS;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,   "hb",             heartbeat_cb },
    { FLUX_MSGTYPE_EVENT,   "mon.commit",     commit_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

static int monsrv_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (flux_event_subscribe (ctx->h, "mon.commit") < 0) {
        flux_log (ctx->h, LOG_ERR, "flux_event_subscribe: %s",strerror (errno));
        return -1;
    }
    if (kvs_watch_dir (h, set_source, ctx, "conf.mon.source") < 0) {
        flux_log (ctx->h, LOG_ERR, "kvs_watch_dir: %s", strerror (errno));
        return -1;
    }
    if (kvs_watch_string (h, "conf.mon.commit-type", set_commit, ctx) < 0) {
        flux_log (ctx->h, LOG_ERR, "kvs_watch_string: %s", strerror (errno));
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
