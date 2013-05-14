/* livesrv.c - node liveness service */

/* This simple implementation could probably be improved.
 * However, it is simple enough that we can be assured of
 * "eventually consistent" liveness state.
 *
 * Activity is orchestrated by the event.sched.trigger.<epoch>
 * multicast message, which marks time in the session.  The <epoch> starts
 * with 1 and increases monotonically.  When event.sched.trigger is received,
 * we send a live.hello.<node>.<epoch> message to our parent.
 *
 * Nodes are only responsible for monitoring their direct descendents, if any.
 * The state of descendents includes the epoch in which a live.hello message
 * was last received.  If this epoch ages too far beyond the current epoch,
 * the node is marked down and an event.live.down.<node> message
 * is multicast so that liveness state across the job can be updated.
 * Similarly, if a live.hello message is received for a node marked down,
 * an event.live.up.<node> message is multicast.
 *
 * A new node initializes the current epoch to 0, liveness state for
 * all nodes to up, and last epoch seen for children to 0.  If the next
 * event.sched.trigger received is for epoch 1, then we cannot have missed
 * any event.live messages because the session state isn't old enough to
 * have created any.  If the next epoch is greater than 1, then we are a
 * late joiner.
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

#define MISSED_TRIGGER_ALLOW    2
#define NOT_MONITORED           (-1)

/* N.B. size=~500K bytes for 100K nodes */
typedef struct {
    int *child_epoch;   /* the state of our children: epoch of last check-in */
    bool *state;        /* the state of everyone: true=up, false=down */
    int epoch;          /* epoch of last received scheduling trigger */
} ctx_t;

/* Send live.hello.<rank> 
 */
static void _send_live_hello (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    json_object *epoch, *o = NULL;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(epoch = json_object_new_int (ctx->epoch)))
        oom ();
    json_object_object_add (o, "epoch", epoch);
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
    json_object *eo, *o = NULL;
    int epoch;

    if (rank < 0 || rank >= p->conf->size)
        goto done;
    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) 
        goto done;
    if (!o || !(eo = json_object_object_get (o, "epoch")))
        goto done;
    epoch = json_object_get_int (eo);

    if (epoch < ctx->child_epoch[rank])
        goto done;

    ctx->child_epoch[rank] = epoch;
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

static void _find_down_nodes (plugin_ctx_t *p, int epoch)
{
    ctx_t *ctx = p->ctx;
    int i;

    for (i = 0; i < p->conf->size; i++) {
        if (ctx->child_epoch[i] == NOT_MONITORED)
            continue;
        if (ctx->epoch - ctx->child_epoch[i] > MISSED_TRIGGER_ALLOW) {
            ctx->child_epoch[i] = NOT_MONITORED;
            ctx->state[i] = false;
            cmb_msg_send (p->zs_out_event, NULL, "event.live.down.%d", i);
        }
    }
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

static void _handle_late_joiner (plugin_ctx_t *p, int epoch)
{
    /* No-op for now.  FIXME */
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *arg = NULL;
    ctx_t *ctx = p->ctx;

    if (cmb_msg_match_substr (*zmsg, "event.sched.trigger.", &arg)) {
        int epoch = strtoul (arg, NULL, 10);
        if (ctx->epoch == 0 && epoch > 1)
            _handle_late_joiner (p, epoch);
        ctx->epoch = epoch;
        if (_got_parent (p))
            _send_live_hello (p);
        _find_down_nodes (p, epoch);
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match (*zmsg, "live.query")) {
        _recv_live_query (p, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "live.hello", &arg)) {
        _recv_live_hello (p, arg, zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &arg)) {
        int rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size)
            ctx->state[rank] = true;
        zmsg_destroy (zmsg);
    } else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &arg)) {
        int rank = strtoul (arg, NULL, 10);
        if (rank >= 0 && rank < p->conf->size)
            ctx->state[rank] = true;
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
    ctx->child_epoch = xzmalloc (conf->size * sizeof (ctx->child_epoch[0]));
    ctx->state       = xzmalloc (conf->size * sizeof (ctx->state[0]));
    ctx->epoch = 0;

    sleep (10);

    for (i = 0; i < conf->size; i++)
        ctx->child_epoch[i] = NOT_MONITORED;

    for (i = 0; i < conf->children_len; i++) {
        int rank = conf->children[i];
        if (rank >= 0 && rank < conf->size)
            ctx->child_epoch[rank] = 0;
    }

    for (i = 0; i < conf->size; i++)
        ctx->state[i] = true;

    zsocket_set_subscribe (p->zs_in_event, "event.sched.trigger.");
    zsocket_set_subscribe (p->zs_in_event, "event.live.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    free (ctx->state);
    free (ctx->child_epoch);
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
