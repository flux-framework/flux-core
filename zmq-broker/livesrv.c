/* livesrv.c - node liveness service */

/* FIXME: If a node never starts, its parent doesn't know it is supposed
 * to monitor it.  Need a way to learn session topology.
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

#define MISSED_TRIGGER_ALLOW    3

typedef struct {
    int *live;      /* state of descendents - we actively monitor them */
    bool *live_all; /* session-wide state, "eventually consistent" */
} ctx_t;

static void _event_sched_trigger (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int i, myrank = p->conf->rank;

    /* tell upstream that we're alive */
    if (myrank > 0)
        cmb_msg_send_rt (p->zs_req, NULL, "live.up.%d", myrank);

    /* if self is down, do not issue event.live.down for reparented children */
    if (!ctx->live_all[myrank])
        for (i = 0; i < p->conf->size; i++)
            ctx->live[i] = -1;

    /* age state of descendents, notify of up->down transition, if any */
    for (i = 0; i < p->conf->size; i++) {
        if (ctx->live[i] != -1)
            ctx->live[i]++;
        if (ctx->live[i] > MISSED_TRIGGER_ALLOW) {
            ctx->live[i] = -1;
            ctx->live_all[i] = false;
            cmb_msg_send (p->zs_out_event, NULL, "event.live.down.%d", i);
        }
    }

    if (zmsg && *zmsg) /* we call with NULL zmsg from _init */
        zmsg_destroy (zmsg);
}

static bool _parent_is_down (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    int rank = p->conf->parent[p->srv->parent_cur].rank;

    if (rank < 0 || rank >= p->conf->size || !ctx->live_all[rank])
        return true;
    return false;
}

static void _event_live_up (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int i, rank = strtoul (name, NULL, 10);

    if (rank < 0 || rank >= p->conf->size)
        goto done;
    ctx->live_all[rank] = true;

    /* reparent if current parent is down and candidate just came up */
    if (_parent_is_down (p)) {
        for (i = 0; i < p->conf->parent_len; i++) {
            if (p->conf->parent[i].rank == rank) {
                cmb_msg_send_rt (p->zs_req, NULL, "cmb.reparent.%d", rank);
                break;
            }
        }
    }
done:
    zmsg_destroy (zmsg);
}

static void _event_live_down (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int rank = strtoul (name, NULL, 10);

    if (rank < 0 || rank >= p->conf->size)
        goto done;
    ctx->live_all[rank] = false;

    /* reparent if current parent is down and a candidate is available */
    if (_parent_is_down (p)) {
        int i, nrank;
        for (i = 0; i < p->conf->parent_len; i++) {
            nrank = p->conf->parent[i].rank;
            if (nrank < 0 || nrank >= p->conf->size)
                continue;
            if (ctx->live_all[nrank]) {
                cmb_msg_send_rt (p->zs_req, NULL, "cmb.reparent.%d", nrank);
                break;
            }
        }
    }
done:
    zmsg_destroy (zmsg);
}

static void _live_up (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    int rank = strtoul (name, NULL, 10);

    if (rank < 0 || rank >= p->conf->size)
        goto done;

    /* reset age of descendent state, notify of down->up trandition, if any */
    ctx->live[rank] = 0;
    if (ctx->live_all[rank] == false) {
        ctx->live_all[rank] = true;
        cmb_msg_send (p->zs_out_event, NULL, "event.live.up.%d", rank);
    }
done:
    zmsg_destroy (zmsg);
}

static void _live_query (plugin_ctx_t *p, zmsg_t **zmsg)
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
        if (ctx->live_all[i])
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

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *name = NULL;

    if (cmb_msg_match (*zmsg, "event.sched.trigger"))
        _event_sched_trigger (p, zmsg);
    else if (cmb_msg_match (*zmsg, "live.query"))
        _live_query (p, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "live.up.", &name))
        _live_up (p, name, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.live.up.", &name))
        _event_live_up (p, name, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.live.down.", &name))
        _event_live_down (p, name, zmsg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;
    int i;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->live = xzmalloc (p->conf->size * sizeof (ctx->live[0]));
    ctx->live_all = xzmalloc (p->conf->size * sizeof (ctx->live_all[0]));

    /* Initially set state of descendents to unknown (live[rank] = -1).
     * We don't know who our descendents are so everybody is unknown here.
     * As we hear from descendents we begin to monitor them.
     */
    for (i = 0; i < p->conf->size; i++)
        ctx->live[i] = -1;

    /* Initially set state of all nodes to up (live_all[rank] = true).
     */
    for (i = 0; i < p->conf->size; i++)
        ctx->live_all[i] = true;

    zsocket_set_subscribe (p->zs_in_event, "event.sched.trigger");
    zsocket_set_subscribe (p->zs_in_event, "event.live.");

    _event_sched_trigger (p, NULL);
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    free (ctx->live);
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
