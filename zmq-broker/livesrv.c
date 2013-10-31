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
#include "route.h"
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

typedef struct {
    int rank;
    int epoch;
} child_t;

typedef struct {
    int live_missed_trigger_allow;
    json_object *topology;
    json_object *live_down;
} config_t;

typedef struct {
    zhash_t *kids; /* kids{rank} = child_t */
    int age;
    int epoch;
    config_t conf;
    bool disabled;
} ctx_t;

static bool _alive (plugin_ctx_t *p, int rank)
{
    ctx_t *ctx = p->ctx;
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

static void _child_add (plugin_ctx_t *p, int rank)
{
    ctx_t *ctx = p->ctx;
    char key[16];
    child_t *cp = xzmalloc (sizeof (child_t));

    cp->rank = rank;
    cp->epoch = ctx->epoch;
    snprintf (key, sizeof (key), "%d", cp->rank);
    zhash_update (ctx->kids, key, cp);
    zhash_freefn (ctx->kids, key, free);
}

static void _child_del (plugin_ctx_t *p, char *key)
{
    ctx_t *ctx = p->ctx;
    zhash_delete (ctx->kids, key);
}

static child_t *_child_find_by_rank (plugin_ctx_t *p, int rank)
{
    ctx_t *ctx = p->ctx;
    char key[16];

    snprintf (key, sizeof (key), "%d", rank);
    return (child_t *)zhash_lookup (ctx->kids, key);
}

static void _age_children (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zlist_t *keys;
    char *key;
    child_t *cp;

    if (!(keys = zhash_keys (ctx->kids)))
        oom ();
    while ((key = zlist_pop (keys))) {
        cp = zhash_lookup (ctx->kids, key); 
        if (ctx->epoch > cp->epoch + ctx->conf.live_missed_trigger_allow) {
            if (_alive (p, cp->rank)) {
                if (p->conf->verbose)
                    msg ("aged %d epoch=%d current epoch=%d",
                         cp->rank, cp->epoch, ctx->epoch);
                plugin_log (p, LOG_ALERT,
                    "event.live.down.%d: last seen epoch=%d, current epoch=%d",
                    cp->rank, cp->epoch, ctx->epoch);
                plugin_send_event (p, "event.live.down.%d", cp->rank);
            }
        }
    }
    zlist_destroy (&keys);
}

/* Topology is 2-dim array of integers where topology[rank] = [children].
 * Example: binary tree of 8 nodes, topology = [[1,2],[3,4],[5,6],[7]].
 * 0 parent of 1,2; 1 parent of 3,4; 2 parent of 5,6; 3 is parent of 7.
 * This function returns children of p->conf->rank in an int array,
 * which the caller must free if non-NULL.
 */
static void _get_children_from_topology (plugin_ctx_t *p, int **iap, int *lenp)
{
    ctx_t *ctx = p->ctx;
    json_object *o, *no;
    int *ia = NULL;
    int rank, i, tlen, len = 0;

    if ((o = json_object_array_get_idx (ctx->conf.topology, p->conf->rank))
                            && json_object_get_type (o) == json_type_array
                            && (tlen = json_object_array_length (o)) > 0) {
        ia = xzmalloc (sizeof (int) * tlen);
        for (i = 0; i < tlen; i++) {
            if ((no = json_object_array_get_idx (o, i))
                            && json_object_get_type (no) == json_type_int
                            && (rank = json_object_get_int (no)) > 0
                            && rank < p->conf->size) {
                ia[len++] = rank;
            }
        }
    }
    *iap = ia;
    *lenp = len;
}

static void _child_sync_with_topology (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int *children, len, i, rank;
    zlist_t *keys;
    char *key;

    _get_children_from_topology (p, &children, &len);

    /* del any old */
    if (!(keys = zhash_keys (ctx->kids)))
        oom ();
    while ((key = zlist_pop (keys))) {
        rank = strtoul (key, NULL, 10);
        for (i = 0; i < len; i++)
            if (children[i] == rank)
                break;
        if (i == len)
            _child_del (p, key);
    }
    zlist_destroy (&keys);

    /* add any new */
    for (i = 0; i < len; i++) {
        if (!_child_find_by_rank (p, children[i]))
            _child_add (p, children[i]);
    }
    if (children)
        free (children);
}

/* Send live.hello.<rank> 
 */
static void _send_live_hello (plugin_ctx_t *p, int epoch)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "epoch", epoch);
    plugin_send_request (p, o, "live.hello.%d", p->conf->rank);
    json_object_put (o);
}

/* Receive live.hello.<rank>
 */
static void _recv_live_hello (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int rank = strtoul (arg, NULL, 10);
    json_object *eo, *o = NULL;
    int epoch;
    child_t *cp;

    if (rank < 0 || rank >= p->conf->size)
        goto done;
    if (!(cp = _child_find_by_rank (p, rank)))
        goto done;
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) 
        goto done;
    if (!o || !(eo = json_object_object_get (o, "epoch")))
        goto done;
    epoch = json_object_get_int (eo);
    if (cp->epoch < epoch)
        cp->epoch = epoch;

    if (!_alive (p, cp->rank)) {
        if (ctx->epoch > cp->epoch + ctx->conf.live_missed_trigger_allow) {
            if (p->conf->verbose)
                msg ("ignoring live.hello from %d epoch=%d current epoch=%d",
                     rank, epoch, ctx->epoch);
        } else {
            if (p->conf->verbose)
                msg ("received live.hello from %d epoch=%d current epoch=%d",
                     rank, epoch, ctx->epoch);
            plugin_log (p, LOG_ALERT, "event.live.up.%d", rank);
            plugin_send_event (p, "event.live.up.%d", rank);
        }
    }
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

static void _recv_event_live (plugin_ctx_t *p, bool alive, int rank)
{
    json_object *o, *new = NULL, *old = NULL;
    int i, len = 0;

    assert (plugin_treeroot (p));
    if (rank < 0 || rank > p->conf->size) {
        msg ("%s: received message for bogus rank %d", __FUNCTION__, rank);
        goto done;
    }
    (void)kvs_get (p, "conf.live.down", &old);
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
    kvs_put (p, "conf.live.down", new);
    kvs_commit (p);
done:
    if (old)
        json_object_put (old);
    if (new)
        json_object_put (new);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;
    ctx_t *ctx = p->ctx;

    if (ctx->disabled)
        return;

    if (cmb_msg_match_substr (*zmsg, "event.sched.trigger.", &arg)) {
        ctx->epoch = strtoul (arg, NULL, 10);
        if (!plugin_treeroot (p))
            _send_live_hello (p, ctx->epoch);
        if (ctx->age++ >= ctx->conf.live_missed_trigger_allow)
            _age_children (p);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "live.hello.", &arg))
        _recv_live_hello (p, arg, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &arg))
        _recv_event_live (p, true, strtoul (arg, NULL, 10));
    else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &arg))
        _recv_event_live (p, false, strtoul (arg, NULL, 10));

    if (arg)
        free (arg);
}

static void set_config (const char *key, kvsdir_t dir, void *arg, int errnum)
{
    plugin_ctx_t *p = arg;
    ctx_t *ctx = p->ctx;
    int val;
    json_object *topo, *down = NULL;

    if (errnum > 0) {
        err ("live: %s", key);
        goto invalid;
    }
    if (kvsdir_get_int (dir, "missed-trigger-allow", &val) < 0) {
        err ("live: %s.missed-trigger-allow", key);
        goto invalid;
    }
    if (val < 2 || val > 100) {
        msg ("live: %s.missed-trigger-allow must be >= 2, <= 100", key);
        goto invalid;
    }
    ctx->conf.live_missed_trigger_allow = val; 

    if (kvsdir_get (dir, "topology", &topo) < 0) {
        err ("live: %s.topology", key);
        goto invalid;
    }
    if (ctx->conf.topology)
        json_object_put (ctx->conf.topology);
    json_object_get (topo);
    ctx->conf.topology = topo;
    _child_sync_with_topology (p);

    if (kvsdir_get (dir, "down", &down) < 0 && errno != ENOENT) {
        err ("live: %s.down", key);
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
    if (ctx->disabled) {
        msg ("live: %s values OK, liveness monitoring resumed", key);
        ctx->disabled = false;
    }
    return;
invalid:
    if (!ctx->disabled) {
        msg ("live: %s values invalid, liveness monitoring suspended", key);
        ctx->disabled = true;
    }
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    if (!(ctx->kids = zhash_new ()))
        oom ();
    ctx->disabled = false;

    if (kvs_watch_dir (p, set_config, p, "conf.live") < 0)
        err_exit ("log: %s", "conf.live");

    zsocket_set_subscribe (p->zs_evin, "event.sched.trigger.");
    if (plugin_treeroot (p))
        zsocket_set_subscribe (p->zs_evin, "event.live.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->kids);
    if (ctx->conf.topology)
        json_object_put (ctx->conf.topology);
    if (ctx->conf.live_down)
        json_object_put (ctx->conf.live_down);
    free (ctx);
}

struct plugin_struct livesrv = {
    .name      = "live",
    .initFn    = _init,
    .recvFn    = _recv,
    .finiFn    = _fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
