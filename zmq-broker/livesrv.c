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
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "barriersrv.h"
#include "util.h"

#define MISSED_TRIGGER_ALLOW    3

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    void *zs_in;
    void *zs_in_event;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t t;
    conf_t *conf;
    int *live;
};

static ctx_t ctx = NULL;

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

static void _reply_to_query (const char *sender)
{
    json_object *o, *no, *upo, *dno;
    int i;

    if (!(o = json_object_new_object ()))
        oom ();
    if (!(upo = json_object_new_array ()))
        oom ();
    if (!(dno = json_object_new_array ()))
        oom ();
    for (i = 0; i < ctx->conf->size; i++) {
        if (!(no = json_object_new_int (i)))
            oom ();
        if (ctx->live[i] == -1)
            json_object_array_add (dno, no);
        else
            json_object_array_add (upo, no);
    }
    json_object_object_add (o, "up", upo);
    json_object_object_add (o, "down", dno);

    if (!(no = json_object_new_int (ctx->conf->size)))
        oom ();
    json_object_object_add (o, "nnodes", no);

    cmb_msg_send (ctx->zs_out, o, NULL, 0, 0, "%s", sender);
    json_object_put (o);
}

static void _readmsg (void *socket)
{
    const char *live_up = "live.up.";
    const char *live_query= "live.query";
    const char *event_live_up = "event.live.up.";
    const char *event_live_down = "event.live.down.";
    char *tag = NULL;
    int i, myrank = ctx->conf->rank;
    json_object *o = NULL;

    if (cmb_msg_recv (socket, &tag, &o, NULL, NULL, 0) < 0) {
        fprintf (stderr, "cmb_msg_recv: %s\n", strerror (errno));
        goto done;
    }
    if (!strcmp (tag, "event.sched.trigger")) {
        if (myrank == 0) {
            for (i = 0; i < ctx->conf->size; i++) {
                if (ctx->live[i] != -1)
                    ctx->live[i]++;
                if (ctx->live[i] > MISSED_TRIGGER_ALLOW) {
                    cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, 0,
                                  "event.live.down.%d", i);
                    ctx->live[i] = -1;
                } 
            }
            if (ctx->live[myrank] == -1)
                cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, 0,
                              "event.live.up.%d", myrank);
            ctx->live[myrank] = 0;
        } else {
            cmb_msg_send (ctx->zs_out_tree, NULL, NULL, 0, 0,
                          "live.up.%d", myrank);
        }
    } else if (!strncmp (tag, live_up, strlen (live_up))) {
        int rank = strtoul (tag + strlen (live_up), NULL, 10);
        if (rank < 0 || rank >= ctx->conf->size)
            goto done;
        if (myrank == 0) {
            if (ctx->live[rank] == -1)
                cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, 0,
                              "event.live.up.%d", rank);
            ctx->live[rank] = 0;
        } else {
            cmb_msg_send (ctx->zs_out_tree, NULL, NULL, 0, 0,
                          "live.up.%d", rank);
        }
    } else if (!strncmp (tag, live_query, strlen (live_query))) {
        const char *sender;
        if (_parse_live_query (o, &sender) < 0) {
            fprintf (stderr, "live.query: parse error\n");
            goto done;
        }
        _reply_to_query (sender);
    } else if (!strncmp (tag, event_live_up, strlen (event_live_up))) {
        int rank = strtoul (tag + strlen (event_live_up), NULL, 10);
        if (rank < 0 || rank >= ctx->conf->size)
            goto done;
        if (myrank != 0)
            ctx->live[rank] = 0;
    } else if (!strncmp (tag, event_live_down, strlen (event_live_down))) {
        int rank = strtoul (tag + strlen (event_live_down), NULL, 10);
        if (rank < 0 || rank >= ctx->conf->size)
            goto done;
        if (myrank != 0)
            ctx->live[rank] = -1;
    }
done:
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
}

static void *_thread (void *arg)
{
    zmq_pollitem_t zpa[] = {
{ .socket = ctx->zs_in,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = ctx->zs_in_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    long tmout = -1;

    for (;;) {
        _zmq_poll(zpa, 2, tmout);

        if (zpa[0].revents & ZMQ_POLLIN)
            _readmsg (ctx->zs_in);
        if (zpa[1].revents & ZMQ_POLLIN)
            _readmsg (ctx->zs_in_event);
    }
    return NULL;
}


void livesrv_init (conf_t *conf, void *zctx)
{
    int err;
    int i;

    ctx = xzmalloc (sizeof (struct ctx_struct));

    ctx->live = xzmalloc (conf->size * sizeof (int));
    for (i = 0; i < conf->size; i++)
        ctx->live[i] = -1;
    ctx->conf = conf;

    ctx->zs_out_tree = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_tree, conf->plin_tree_uri);

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe (ctx->zs_in, "live.");

    ctx->zs_in_event = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in_event, conf->plout_event_uri);
    _zmq_subscribe (ctx->zs_in_event, "event.sched.trigger");
    _zmq_subscribe (ctx->zs_in_event, "event.live.");

    err = pthread_create (&ctx->t, NULL, _thread, NULL);
    if (err) {
        fprintf (stderr, "livesrv_init: pthread_create: %s\n", strerror (err));
        exit (1);
    }
}

void livesrv_fini (conf_t *conf)
{
    int err;

    err = pthread_join (ctx->t, NULL);
    if (err) {
        fprintf (stderr, "livesrv_fini: pthread_join: %s\n", strerror (err));
        exit (1);
    }
    _zmq_close (ctx->zs_in);
    _zmq_close (ctx->zs_in_event);
    _zmq_close (ctx->zs_out);
    _zmq_close (ctx->zs_out_event);
    _zmq_close (ctx->zs_out_tree);

    free (ctx->live);
    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
