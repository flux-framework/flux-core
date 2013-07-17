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

#include "zmq.h"
#include "cmb.h"
#include "route.h"
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "livesrv.h"

typedef struct {
    int rank;
    int epoch;
} child_t;

typedef struct {
    bool *state;/* the state of everyone: true=up, false=down */
    int age;
    zhash_t *kids; /* kids{rank} = child_t */
    int live_missed_trigger_allow;
    json_object *topology;
} ctx_t;

static void _child_add (zhash_t *kids, int rank, int epoch)
{
    char key[16];
    child_t *cp = xzmalloc (sizeof (child_t));

    cp->rank = rank;
    cp->epoch = epoch;
    snprintf (key, sizeof (key), "%d", cp->rank);
    zhash_update (kids, key, cp);
    zhash_freefn (kids, key, free);
}

static child_t *_child_find_by_rank (zhash_t *kids, int rank)
{
    char key[16];

    snprintf (key, sizeof (key), "%d", rank);
    return (child_t *)zhash_lookup (kids, key);
}

typedef struct {
    int epoch;
    child_t *child;
    plugin_ctx_t *p;
} aarg_t;

static int _match_aged (const char *key, void *item, void *arg)
{
    aarg_t *ap = arg;
    ctx_t *ctx = ap->p->ctx;
    child_t *cp = item;
    if (ap->epoch > cp->epoch + ctx->live_missed_trigger_allow)
        ap->child = cp;
    return (ap->child != NULL);
}

static child_t *_child_find_aged (plugin_ctx_t *p, zhash_t *kids, int epoch)
{
    aarg_t aarg = { .epoch = epoch, .child = NULL, .p = p };

    zhash_foreach (kids, _match_aged, &aarg);
    return aarg.child;
}

static void _child_del (zhash_t *kids, int rank)
{
    char key[16];

    snprintf (key, sizeof (key), "%d", rank);
    zhash_delete (kids, key);
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

    if ((o = json_object_array_get_idx (ctx->topology, p->conf->rank))
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

/* Synchronize ctx->kids with ctx->topology after change in topology.
 */
static void _child_update_all (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int *children, len, i, rank;
    int epoch = 0; /* FIXME this is wrong, but will we need it? */
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
            zhash_delete (ctx->kids, key);
    }
    zlist_destroy (&keys);

    /* add any new */
    for (i = 0; i < len; i++) {
        if (!_child_find_by_rank (ctx->kids, children[i]))
            _child_add (ctx->kids, children[i], epoch);
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
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) 
        goto done;
    if (!o || !(eo = json_object_object_get (o, "epoch")))
        goto done;
    epoch = json_object_get_int (eo);

    cp = _child_find_by_rank (ctx->kids, rank);
    if (cp) {
        cp->epoch = epoch;
    } else
        _child_add (ctx->kids, rank, epoch);

    if (ctx->state[rank] == false) {
        if (p->conf->verbose)
            msg ("heard from rank %d, marking up", rank);
        ctx->state[rank] = true;
        plugin_log (p, CMB_LOG_ALERT, "event.live.up.%d", rank);
        plugin_send_event (p, "event.live.up.%d", rank);
    }
done:
    if (o)
        json_object_put (o);
    zmsg_destroy (zmsg);
}

/* Receive live.query (request)
 * Send live.query (response)
 */
static void _recv_live_query (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    json_object *no, *upo, *dno, *o = util_json_object_new_object ();
    int i;

    if (!(upo = json_object_new_array ()))
        oom ();
    if (!(dno = json_object_new_array ()))
        oom ();
    for (i = 0; i < p->conf->size; i++) {
        if (!(no = json_object_new_int (i)))
            oom ();
        if (ctx->state[i])
            json_object_array_add (upo, no);
        else
            json_object_array_add (dno, no);
    }
    json_object_object_add (o, "up", upo);
    json_object_object_add (o, "down", dno);
    util_json_object_add_int (o, "nnodes", p->conf->size);
    plugin_send_response (p, zmsg, o);
    json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
}

static bool _got_parent (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int rank;

    if (p->conf->parent_len == 0)
        return false;
    rank = p->conf->parent[p->srv->parent_cur].rank;
    if (rank < 0 || rank >= p->conf->size)
        return false;
    return ctx->state[rank];
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;
    ctx_t *ctx = p->ctx;
    int epoch, rank;
    child_t *cp;

    /* On the clock tick, we must:
     * - say hello to our parent
     * - age our children
     */
    if (cmb_msg_match_substr (*zmsg, "event.sched.trigger.", &arg)) {
        epoch = strtoul (arg, NULL, 10);
        if (_got_parent (p))
            _send_live_hello (p, epoch);
        if (ctx->age++ >= ctx->live_missed_trigger_allow) {
            while ((cp = _child_find_aged (p, ctx->kids, epoch))) {
                if (cp->rank >= 0 && cp->rank < p->conf->size) {
                    plugin_log (p, CMB_LOG_ALERT,
                        "event.live.down.%d: last seen %d, current %d",
                        cp->rank, cp->epoch, epoch);
                    plugin_send_event (p, "event.live.down.%d", cp->rank);
                    ctx->state[cp->rank] = false;
                }
                _child_del (ctx->kids, cp->rank);
            }
        }
        zmsg_destroy (zmsg);

    } else if (cmb_msg_match (*zmsg, "live.query")) {
        _recv_live_query (p, zmsg);

    } else if (cmb_msg_match_substr (*zmsg, "live.hello.", &arg)) {
        _recv_live_hello (p, arg, zmsg);

    /* When a node transitions up|down, we must:
     * - mark it up|down in our state
     */
    } else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size)
            ctx->state[rank] = true;
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size)
            ctx->state[rank] = false;
        zmsg_destroy (zmsg);
    }

    if (arg)
        free (arg);
}

static void _set_live_missed_trigger_allow (const char *key, json_object *o,
                                            void *arg)
{
    plugin_ctx_t *p = arg;
    ctx_t *ctx = p->ctx;
    int i;

    if (!o)
        msg_exit ("live: %s is not set", key);
    i = json_object_get_int (o);
    if (i < 2 || i > 100)
        msg_exit ("live: bad %s value: %d", key, i);
    ctx->live_missed_trigger_allow = i; 
}

static void _set_topology (const char *key, json_object *o, void *arg)
{
    plugin_ctx_t *p = arg;
    ctx_t *ctx = p->ctx;

    if (!o)
        msg_exit ("live: %s is not set", key);
    if (json_object_get_type (o) != json_type_array)
        msg_exit ("live: %s is not type array", key);
    if (ctx->topology)
        json_object_put (ctx->topology);
    ctx->topology = o;

    _child_update_all (p);
}

static void _init (plugin_ctx_t *p)
{
    conf_t *conf = p->conf;
    ctx_t *ctx;
    int i;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->state = xzmalloc (conf->size * sizeof (ctx->state[0]));
    if (!(ctx->kids = zhash_new ()))
        oom ();

    plugin_conf_watch (p, "live.missed.trigger.allow",
                      _set_live_missed_trigger_allow, p);
    plugin_conf_watch (p, "topology", _set_topology, p);

    for (i = 0; i < conf->size; i++)
        ctx->state[i] = true;

    zsocket_set_subscribe (p->zs_evin, "event.sched.trigger.");
    zsocket_set_subscribe (p->zs_evin, "event.live.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->kids);
    if (ctx->topology)
        json_object_put (ctx->topology);
    free (ctx->state);
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
