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
    int parent;
} child_t;

typedef struct {
    bool *state;/* the state of everyone: true=up, false=down */
    int age;
    zhash_t *kids;
    int live_missed_trigger_allow;
} ctx_t;

static void _child_add (zhash_t *kids, int rank, int epoch, int parent)
{
    char key[16];
    child_t *cp = xzmalloc (sizeof (child_t));

    cp->rank = rank;
    cp->epoch = epoch;
    cp->parent = parent;
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
    int parent;
    child_t *child;
} parg_t;

static int _match_parent (const char *key, void *item, void *arg)
{
    parg_t *pp = arg;
    child_t *cp = item;
    if (pp->parent == cp->parent)
        pp->child = cp;
    return (pp->child != NULL);
}

static child_t *_child_find_by_parent (zhash_t *kids, int parent)
{
    parg_t parg = { .parent = parent, .child = NULL };

    zhash_foreach (kids, _match_parent, &parg);
    return parg.child;
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

/* Send live.hello.<rank> 
 */
static void _send_live_hello (plugin_ctx_t *p, int epoch)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "epoch", epoch);
    assert (p->conf->parent_len > 0);
    util_json_object_add_int (o, "parent", p->conf->parent[0].rank);
    plugin_send_request (p, o, "live.hello.%d", p->conf->rank);
    json_object_put (o);
}

/* Receive live.hello.<rank>
 */
static void _recv_live_hello (plugin_ctx_t *p, char *arg, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int rank = strtoul (arg, NULL, 10);
    json_object *po, *eo, *o = NULL;
    int epoch, parent;
    child_t *cp;

    if (rank < 0 || rank >= p->conf->size)
        goto done;
    if (cmb_msg_decode (*zmsg, NULL, &o) < 0) 
        goto done;
    if (!o || !(eo = json_object_object_get (o, "epoch"))
           || !(po = json_object_object_get (o, "parent")))
        goto done;
    epoch = json_object_get_int (eo);
    parent = json_object_get_int (po);

    cp = _child_find_by_rank (ctx->kids, rank);
    if (cp) {
        cp->epoch = epoch;
    } else
        _child_add (ctx->kids, rank, epoch, parent);

    if (ctx->state[rank] == false) {
        if (p->conf->verbose)
            msg ("heard from rank %d, marking up", rank);
        ctx->state[rank] = true;
        plugin_log (p, CMB_LOG_DEBUG, "event.live.up.%d", rank);
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

    /* When a node transitions up, we must:
     * - mark it up in our state
     * - stop monitoring kids whose real parent came back up
     */
    } else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size) {
            ctx->state[rank] = true;
            while ((cp = _child_find_by_parent (ctx->kids, rank)))
                _child_del (ctx->kids, cp->rank);
        }
        zmsg_destroy (zmsg);

    /* When a node transitions down, we must:
     * - mark it down in our state
     */
    } else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size)
            ctx->state[rank] = false;
        zmsg_destroy (zmsg);
    }

    if (arg)
        free (arg);
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

    ctx->live_missed_trigger_allow = plugin_conf_get_int (p,
                                    "live.missed.trigger.allow");
    if (ctx->live_missed_trigger_allow < 2)
        msg_exit ("live: live.missed.trigger.allow should be >= 2");

    for (i = 0; i < conf->size; i++)
        ctx->state[i] = true;

    for (i = 0; i < conf->live_children_len; i++)
        _child_add (ctx->kids, conf->live_children[i], 0, conf->rank);

    zsocket_set_subscribe (p->zs_evin, "event.sched.trigger.");
    zsocket_set_subscribe (p->zs_evin, "event.live.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    zhash_destroy (&ctx->kids);
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
