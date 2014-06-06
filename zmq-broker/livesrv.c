/* livesrv.c - node liveness service */

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

typedef struct {
    int rank;
    int epoch;
} child_t;

typedef struct {
    int live_missed_hb_allow;
    json_object *topology;
    json_object *live_down;
} config_t;

typedef struct {
    zhash_t *kids; /* kids{rank} = child_t */
    int age;
    int epoch;
    config_t conf;
    bool disabled;
    flux_t h;
} ctx_t;

static int live_event_send (flux_t h, int rank, bool alive);

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->kids);
    if (ctx->conf.topology)
        json_object_put (ctx->conf.topology);
    if (ctx->conf.live_down)
        json_object_put (ctx->conf.live_down);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "livesrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->kids = zhash_new ()))
            oom ();
        ctx->disabled = false;
        ctx->h = h;
        flux_aux_set (h, "livesrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static bool alive (ctx_t *ctx, int rank)
{
    int i, len;
    bool alive = true;
    json_object *o;

    if (ctx->conf.live_down) {
        len = json_object_array_length (ctx->conf.live_down);
        for (i = 0; i < len; i++) {
            o = json_object_array_get_idx (ctx->conf.live_down, i);
            if (json_object_get_int (o) == rank) {
                alive = false;
                break;
            }
        }
    }  
    return alive;
}

static void child_add (ctx_t *ctx, int rank)
{
    char key[16];
    child_t *cp = xzmalloc (sizeof (child_t));

    cp->rank = rank;
    cp->epoch = ctx->epoch;
    snprintf (key, sizeof (key), "%d", cp->rank);
    zhash_update (ctx->kids, key, cp);
    zhash_freefn (ctx->kids, key, free);
}

static void child_del (ctx_t *ctx, char *key)
{
    zhash_delete (ctx->kids, key);
}

static child_t *child_find_by_rank (ctx_t *ctx, int rank)
{
    char key[16];

    snprintf (key, sizeof (key), "%d", rank);
    return (child_t *)zhash_lookup (ctx->kids, key);
}

static void age_children (ctx_t *ctx)
{
    zlist_t *keys;
    char *key;
    child_t *cp;

    if (!(keys = zhash_keys (ctx->kids)))
        oom ();
    while ((key = zlist_pop (keys))) {
        cp = zhash_lookup (ctx->kids, key); 
        if (ctx->epoch > cp->epoch + ctx->conf.live_missed_hb_allow) {
            if (alive (ctx, cp->rank)) {
                //msg ("aged %d epoch=%d current epoch=%d",
                //     cp->rank, cp->epoch, ctx->epoch);
                flux_log (ctx->h, LOG_ALERT,
                    "node %d is down: last seen epoch=%d, current epoch=%d",
                    cp->rank, cp->epoch, ctx->epoch);
                if (live_event_send (ctx->h, cp->rank, false) < 0)
                    err_exit ("%s: live_event_send", __FUNCTION__);
            }
        }
    }
    zlist_destroy (&keys);
}

/* Topology is 2-dim array of integers where topology[rank] = [children].
 * Example: binary tree of 8 nodes, topology = [[1,2],[3,4],[5,6],[7]].
 * 0 parent of 1,2; 1 parent of 3,4; 2 parent of 5,6; 3 is parent of 7.
 * This function returns children of my rank in an int array,
 * which the caller must free if non-NULL.
 */
static void get_children_from_topology (ctx_t *ctx, int **iap, int *lenp)
{
    json_object *o, *no;
    int *ia = NULL;
    int rank, i, tlen, len = 0;

    if ((o = json_object_array_get_idx (ctx->conf.topology, flux_rank (ctx->h)))
                            && json_object_get_type (o) == json_type_array
                            && (tlen = json_object_array_length (o)) > 0) {
        ia = xzmalloc (sizeof (int) * tlen);
        for (i = 0; i < tlen; i++) {
            if ((no = json_object_array_get_idx (o, i))
                            && json_object_get_type (no) == json_type_int
                            && (rank = json_object_get_int (no)) > 0
                            && rank < flux_size (ctx->h)) {
                ia[len++] = rank;
            }
        }
    }
    *iap = ia;
    *lenp = len;
}

static void child_sync_with_topology (ctx_t *ctx)
{
    int *children, len, i, rank;
    zlist_t *keys;
    char *key;

    get_children_from_topology (ctx, &children, &len);

    /* del any old */
    if (!(keys = zhash_keys (ctx->kids)))
        oom ();
    while ((key = zlist_pop (keys))) {
        rank = strtoul (key, NULL, 10);
        for (i = 0; i < len; i++)
            if (children[i] == rank)
                break;
        if (i == len)
            child_del (ctx, key);
    }
    zlist_destroy (&keys);

    /* add any new */
    for (i = 0; i < len; i++) {
        if (!child_find_by_rank (ctx, children[i]))
            child_add (ctx, children[i]);
    }
    if (children)
        free (children);
}

/* Send live.hello
 */
static int hello_request_send (flux_t h, int epoch, int rank)
{
    json_object *o = util_json_object_new_object ();
    int rc;

    util_json_object_add_int (o, "epoch", epoch);
    util_json_object_add_int (o, "rank", rank);
    rc = flux_request_send (h, o, "live.hello");
    json_object_put (o);
    return rc;
}

static int live_event_send (flux_t h, int rank, bool alive)
{
    json_object *o = util_json_object_new_object ();
    int rc;
    util_json_object_add_int (o, "rank", rank);
    util_json_object_add_boolean (o, "alive", alive);
    rc = flux_event_send (h, o, "live", rank);
    json_object_put (o);
    return rc;
}

/* Receive live.hello
 */
static int hello_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    int rank;
    json_object *o = NULL;
    int epoch;
    child_t *cp;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || !o
                || util_json_object_get_int (o, "epoch", &epoch) < 0
                || util_json_object_get_int (o, "rank", &rank) < 0) {
        goto done;
    }
    if (rank < 0 || rank >= flux_size (ctx->h))
        goto done;
    if (!(cp = child_find_by_rank (ctx, rank)))
        goto done;
    if (cp->epoch < epoch)
        cp->epoch = epoch;

    if (!alive (ctx, cp->rank)) {
        if (ctx->epoch > cp->epoch + ctx->conf.live_missed_hb_allow) {
            //msg ("ignoring live.hello from %d epoch=%d current epoch=%d",
            //     rank, epoch, ctx->epoch);
        } else {
            //msg ("received live.hello from %d epoch=%d current epoch=%d",
            //     rank, epoch, ctx->epoch);
            flux_log (ctx->h, LOG_ALERT, "node %d is UP", rank);
            if (live_event_send (ctx->h, rank, true) < 0)
                err_exit ("%s: live_event_send", __FUNCTION__);
        }
    }
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
    return 0;
}

static void setlive (ctx_t *ctx, bool alive, int rank)
{
    json_object *o, *new = NULL, *old = NULL;
    int i, len = 0;

    assert (flux_treeroot (ctx->h));
    if (rank < 0 || rank > flux_size (ctx->h)) {
        msg ("%s: received message for bogus rank %d", __FUNCTION__, rank);
        goto done;
    }
    (void)kvs_get (ctx->h, "conf.live.down", &old);
    if (!(new = json_object_new_array ()))
        oom ();
    if (!alive) {
        if (!(o = json_object_new_int (rank)))
            oom ();
        json_object_array_add (new, o);
    }
    if (old) {
        len = json_object_array_length (old);
        for (i = 0; i < len; i++) {
            o = json_object_array_get_idx (old, i);
            if (json_object_get_int (o) != rank) {
                json_object_get (o);
                json_object_array_add (new, o);
            }
        }
    }
    kvs_put (ctx->h, "conf.live.down", new);
    kvs_commit (ctx->h);
done:
    if (old)
        json_object_put (old);
    if (new)
        json_object_put (new);
}

static int live_event_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;
    int rank;
    bool alive;
    
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
           || util_json_object_get_int (o, "rank", &rank) < 0
           || util_json_object_get_boolean (o, "alive", &alive) < 0) {
        goto done;        
    }
    setlive (ctx, alive, rank);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static int hb_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
            || util_json_object_get_int (o, "epoch", &ctx->epoch) < 0) {
        flux_log (h, LOG_ERR, "received mangled heartbeat event");
        goto done;        
    }
    if (!flux_treeroot (ctx->h)) {
        if (hello_request_send (ctx->h, ctx->epoch, flux_rank (h)) < 0)
            flux_log (h, LOG_ERR, "hello_request_send: %s", strerror (errno));
    }
    if (ctx->age++ >= ctx->conf.live_missed_hb_allow)
        age_children (ctx);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static void set_config (const char *path, kvsdir_t dir, void *arg, int errnum)
{
    ctx_t *ctx = arg;
    int val;
    json_object *topo, *down = NULL;
    char *key;

    if (errnum != 0) {
        err ("live: %s", path);
        goto invalid;
    }
    key = kvsdir_key_at (dir, "missed-hb-allow");    
    if (kvs_get_int (ctx->h, key, &val) < 0) {
        err ("live: %s", key);
        goto invalid;
    }
    if (val < 2 || val > 100) {
        msg ("live: %s must be >= 2, <= 100", key);
        goto invalid;
    }
    ctx->conf.live_missed_hb_allow = val; 
    free (key);

    key = kvsdir_key_at (dir, "topology");    
    if (kvs_get (ctx->h, key, &topo) < 0) {
        err ("live: %s", key);
        goto invalid;
    }
    if (ctx->conf.topology)
        json_object_put (ctx->conf.topology);
    json_object_get (topo);
    ctx->conf.topology = topo;
    child_sync_with_topology (ctx);
    free (key);

    key = kvsdir_key_at (dir, "down");    
    if (kvs_get (ctx->h, key, &down) < 0 && errno != ENOENT) {
        err ("live: %s", key);
        goto invalid;
    }
    if (ctx->conf.live_down) {
        json_object_put (ctx->conf.live_down);
        ctx->conf.live_down = NULL;
    }
    if (down) {
        json_object_get (down);
        ctx->conf.live_down = down;
    }
    free (key);

    if (ctx->disabled) {
        msg ("live: %s values OK, liveness monitoring resumed", path);
        ctx->disabled = false;
    }
    return;
invalid:
    if (!ctx->disabled) {
        msg ("live: %s values invalid, liveness monitoring suspended", path);
        ctx->disabled = true;
    }
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_REQUEST,     "live.hello",          hello_request_cb },
    { FLUX_MSGTYPE_EVENT,       "hb",                  hb_cb },
    { FLUX_MSGTYPE_EVENT,       "live",                live_event_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (kvs_watch_dir (h, set_config, ctx, "conf.live") < 0) {
        flux_log (h, LOG_ERR, "kvs_watch_dir: %s", strerror (errno));
        return -1;
    }
    if (flux_event_subscribe (h, "hb") < 0) {
        flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        return -1;
    }
    if (flux_treeroot (h)) {
        if (flux_event_subscribe (h, "live") < 0) {
            flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
            return -1;
        }
    }
    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("live");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
