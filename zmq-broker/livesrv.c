/* livesrv.c - node liveness service */

/* This simple implementation could probably be improved.
 * However, it is simple enough that we can be assured of
 * "eventually consistent" liveness state.
 *
 * Activity is orchestrated by the event.sched.trigger.<epoch>
 * multicast message, which marks time in the session.  The <epoch> starts
 * with 1 and increases monotonically.  When event.sched.trigger is received,
 * we send a live.hello.<node> message to our parent.
 *
 * Nodes are only responsible for monitoring their direct children, if any.
 * The state of children includes the epoch in which a live.hello message
 * was last received.  If this epoch ages too far beyond the current epoch,
 * the node is marked down and an event.live.down.<node> message
 * is multicast so that liveness state across the job can be updated.
 * Similarly, if a live.hello message is received for a node marked down,
 * an event.live.up.<node> message is multicast.
 *
 * A new node initializes the current epoch to 0, liveness state for
 * all nodes to up, and last epoch seen for children to 0.  If the next
 * event.sched.trigger received is for epoch 1, then we cannot have missed
 * any event.live messages because the session isn't old enough to have
 * created any.  If the next epoch is greater than 1, then we are a late
 * joiner.
 *
 * FIXME: handle late joiner
 * 1) Request state from parent.
 * 2) If parent takes too long, time out and try alternate parent.
 * 3) Any event.live.up/down messages received must be stored and replayed
 * after obtaining the state.
 * 4) Requests for state should be stored and answered after #1
 */

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
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "livesrv.h"

#define MISSED_EPOCH_ALLOW    2

typedef struct {
    int rank;
    int epoch;  /* epoch of last hello */
    int parent; /* in case of adoptees */
} child_t;

typedef struct {
    bool *state;/* the state of everyone: true=up, false=down */
    int epoch;  /* epoch of last received scheduling trigger (0 initially) */
    zhash_t *kids;
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

static void _route_add (plugin_ctx_t *p, char *dst, char *gw)
{
    json_object *no, *o = NULL;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_string (gw)))
        oom ();
    json_object_object_add (o, "gw", no);
    cmb_msg_send_rt (p->zs_req, o, "cmb.route.add.%s", dst);
    json_object_put (o);
}

static void _route_del (plugin_ctx_t *p, char *dst, char *gw)
{
    json_object *no, *o = NULL;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_string (gw)))
        oom ();
    json_object_object_add (o, "gw", no);
    cmb_msg_send_rt (p->zs_req, o, "cmb.route.del.%s", dst);
}

static void _route_add_rank (plugin_ctx_t *p, int dst_rank, int gw_rank)
{
    char dst[16], gw[16];
    snprintf (dst, sizeof (dst), "%d", dst_rank);
    snprintf (gw, sizeof (gw), "%d", gw_rank);
    _route_add (p, dst, gw);
}

static void _route_del_rank (plugin_ctx_t *p, int dst_rank, int gw_rank)
{
    char dst[16], gw[16];
    snprintf (dst, sizeof (dst), "%d", dst_rank);
    snprintf (gw, sizeof (gw), "%d", gw_rank);
    _route_del (p, dst, gw);
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
} aarg_t;

static int _match_aged (const char *Key, void *item, void *arg)
{
    aarg_t *ap = arg;
    child_t *cp = item;
    if (ap->epoch > cp->epoch + MISSED_EPOCH_ALLOW)
        ap->child = cp;
    return (ap->child != NULL);
}

static child_t *_child_find_aged (zhash_t *kids, int epoch)
{
    aarg_t aarg = { .epoch = epoch, .child = NULL };

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
static void _send_live_hello (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    json_object *no, *o = NULL;

    if (!(o = json_object_new_object ()))
        oom ();

    if (!(no = json_object_new_int (ctx->epoch)))
        oom ();
    json_object_object_add (o, "epoch", no);

    assert (p->conf->parent_len > 0);
    if (!(no = json_object_new_int (p->conf->parent[0].rank)))
        oom ();
    json_object_object_add (o, "parent", no);

    cmb_msg_send_rt (p->zs_req, o, "live.hello.%d", p->conf->rank);
    if (o)
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
    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) 
        goto done;
    if (!o || !(eo = json_object_object_get (o, "epoch"))
           || !(po = json_object_object_get (o, "parent")))
        goto done;
    epoch = json_object_get_int (eo);
    parent = json_object_get_int (po);

    cp = _child_find_by_rank (ctx->kids, rank);
    if (cp) {
        if (epoch < cp->epoch)
            goto done;
        cp->epoch = epoch;
    } else {
        _child_add (ctx->kids, rank, parent, epoch);
        _route_add_rank (p, rank, rank);
    }

    if (ctx->state[rank] == false) {
        ctx->state[rank] = true;
        cmb_msg_send (p->zs_out_event, NULL, "event.live.up.%d", rank);
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
    json_object *o, *no, *upo, *dno;
    int i;

    if (!(o = json_object_new_object ()))
        oom ();
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

    if (!(no = json_object_new_int (p->conf->size)))
        oom ();
    json_object_object_add (o, "nnodes", no);
    if (cmb_msg_rep_json (*zmsg, o) < 0)
        goto done;
    if (zmsg_send (zmsg, p->zs_out) < 0)
        err ("zmsg_send");
done:
    if (o)
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

static void _reparent (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int i, rank;

    for (i = 0; i < p->conf->parent_len; i++) {
        rank = p->conf->parent[i].rank;
        if (i != p->srv->parent_cur && ctx->state[rank] == true) {
            cmb_msg_send_rt (p->zs_req, NULL, "cmb.reparent.%d", rank);
            break;
        }
    }
}

static void _handle_late_joiner (plugin_ctx_t *p, int epoch)
{
    /* No-op for now.  FIXME */
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;
    ctx_t *ctx = p->ctx;
    int epoch, rank;
    child_t *cp;

    /* On the clock tick, we must:
     * - notice if we are a late joiner and "catch up" our state
     * - say hello to our parent
     * - age our children
     */
    if (cmb_msg_match_substr (*zmsg, "event.sched.trigger.", &arg)) {
        epoch = strtoul (arg, NULL, 10);
        if (ctx->epoch == 0 && epoch > 1)
            _handle_late_joiner (p, epoch);
        ctx->epoch = epoch;
        if (_got_parent (p))
            _send_live_hello (p);
        while ((cp = _child_find_aged (ctx->kids, epoch))) {
            if (cp->rank >= 0 && cp->rank < p->conf->size) {
                cmb_msg_send (p->zs_out_event, NULL, "event.live.down.%d",
                              cp->rank);
                ctx->state[cp->rank] = false;
            }
            _child_del (ctx->kids, cp->rank);
            _route_del_rank (p, cp->rank, cp->rank);
        }
        zmsg_destroy (zmsg);

    } else if (cmb_msg_match (*zmsg, "live.query")) {
        _recv_live_query (p, zmsg);

    } else if (cmb_msg_match_substr (*zmsg, "live.hello.", &arg)) {
        _recv_live_hello (p, arg, zmsg);

    /* When a node transitions up, we must:
     * - mark it up in our state
     * - stop monitoring kids whose real parent came back up
     * - if primary parent, reparent
     */
    } else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size) {
            ctx->state[rank] = true;
            while ((cp = _child_find_by_parent (ctx->kids, rank))) {
                _child_del (ctx->kids, cp->rank);
                _route_del_rank (p, cp->rank, cp->rank);
            }
            if (p->conf->parent_len > 0 && p->conf->parent[0].rank == rank)
                _reparent (p);
        }
        zmsg_destroy (zmsg);

    /* When a node transitions down, we must:
     * - mark it down in our state
     * - if current parent, reparent
     */
    } else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &arg)) {
        rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size) {
            ctx->state[rank] = false;
            if (p->conf->parent_len > 0 && p->conf->parent[p->srv->parent_cur].rank == rank)
                _reparent (p);
        }
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
    ctx->epoch = 0;

    for (i = 0; i < conf->size; i++)
        ctx->state[i] = true;

    for (i = 0; i < conf->children_len; i++) {
        _child_add (ctx->kids, conf->children[i], ctx->epoch, conf->rank);
        _route_add_rank (p, conf->children[i], conf->children[i]);
    }

    zsocket_set_subscribe (p->zs_in_event, "event.sched.trigger.");
    zsocket_set_subscribe (p->zs_in_event, "event.live.");
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
