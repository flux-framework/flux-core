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
#include "cmbd.h"
#include "util.h"
#include "log.h"
#include "plugin.h"

#include "livesrv.h"

#define MISSED_TRIGGER_ALLOW    3

typedef struct {
    int *live;
} ctx_t;

static int _parse_live_query (json_object *o, const char **sp)
{
    json_object *sender;

    if (!o)
        goto error;
    sender = json_object_object_get (o, "sender");
    if (!sender)
        goto error;
    *sp = json_object_get_string (sender);
    return 0;
error:
    return -1;
}

static void _reply_to_query (plugin_ctx_t *p, const char *sender)
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
        if (ctx->live[i] == -1)
            json_object_array_add (dno, no);
        else
            json_object_array_add (upo, no);
    }
    json_object_object_add (o, "up", upo);
    json_object_object_add (o, "down", dno);

    if (!(no = json_object_new_int (p->conf->size)))
        oom ();
    json_object_object_add (o, "nnodes", no);

    cmb_msg_send_long (p->zs_out, o, NULL, 0, "%s", sender);
    json_object_put (o);
}

static void _recv (plugin_ctx_t *p, zmsg_t *zmsg)
{
    ctx_t *ctx = p->ctx;
    const char *live_up = "live.up.";
    const char *live_query= "live.query";
    const char *event_live_up = "event.live.up.";
    const char *event_live_down = "event.live.down.";
    char *tag = NULL;
    int i, myrank = p->conf->rank;
    json_object *o = NULL;

    if (cmb_msg_decode (zmsg, &tag, &o, NULL, NULL) < 0) {
        err ("livesrv: recv");
        goto done; 
    }

    if (!strcmp (tag, "event.sched.trigger")) {
        if (myrank == 0) {
            for (i = 0; i < p->conf->size; i++) {
                if (ctx->live[i] != -1)
                    ctx->live[i]++;
                if (ctx->live[i] > MISSED_TRIGGER_ALLOW) {
                    cmb_msg_send (p->zs_out_event, "event.live.down.%d", i);
                    ctx->live[i] = -1;
                } 
            }
            if (ctx->live[myrank] == -1)
                cmb_msg_send (p->zs_out_event, "event.live.up.%d", myrank);
            ctx->live[myrank] = 0;
        } else {
            cmb_msg_send (p->zs_out_tree, "live.up.%d", myrank);
        }
    } else if (!strncmp (tag, live_up, strlen (live_up))) {
        int rank = strtoul (tag + strlen (live_up), NULL, 10);
        if (rank < 0 || rank >= p->conf->size)
            goto done;
        if (myrank == 0) {
            if (ctx->live[rank] == -1)
                cmb_msg_send (p->zs_out_event, "event.live.up.%d", rank);
            ctx->live[rank] = 0;
        } else {
            cmb_msg_send (p->zs_out_tree, "live.up.%d", rank);
        }
    } else if (!strncmp (tag, live_query, strlen (live_query))) {
        const char *sender;
        if (_parse_live_query (o, &sender) < 0) {
            fprintf (stderr, "live.query: parse error\n");
            goto done;
        }
        _reply_to_query (p, sender);
    } else if (!strncmp (tag, event_live_up, strlen (event_live_up))) {
        int rank = strtoul (tag + strlen (event_live_up), NULL, 10);
        if (rank < 0 || rank >= p->conf->size)
            goto done;
        if (myrank != 0)
            ctx->live[rank] = 0;
    } else if (!strncmp (tag, event_live_down, strlen (event_live_down))) {
        int rank = strtoul (tag + strlen (event_live_down), NULL, 10);
        if (rank < 0 || rank >= p->conf->size)
            goto done;
        if (myrank != 0)
            ctx->live[rank] = -1;
    }
done:
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
    if (zmsg)
        zmsg_destroy (&zmsg);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;
    int i;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    ctx->live = xzmalloc (p->conf->size * sizeof (int));

    for (i = 0; i < p->conf->size; i++)
        ctx->live[i] = -1;

    zsocket_set_subscribe (p->zs_in, "live.");
    zsocket_set_subscribe (p->zs_in_event, "event.sched.trigger");
    zsocket_set_subscribe (p->zs_in_event, "event.live.");
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    free (ctx->live);
    free (ctx);
}

struct plugin_struct livesrv = {
    .initFn    = _init,
    .recvFn    = _recv,
    .finiFn    = _fini,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
