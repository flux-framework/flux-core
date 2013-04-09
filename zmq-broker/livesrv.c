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
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t t;
    conf_t *conf;
    int *live;
};

static ctx_t ctx = NULL;

static bool _readmsg (void)
{
    const char *live_up = "live.up.";
    bool shutdown = false;
    char *tag = NULL;
    int i;

    if (cmb_msg_recv (ctx->zs_in, &tag, NULL, NULL, 0) < 0) {
        fprintf (stderr, "cmb_msg_recv: %s\n", strerror (errno));
        goto done;
    }
    if (!strcmp (tag, "event.cmb.shutdown")) {
        shutdown = true;

    } else if (!strcmp (tag, "event.sched.trigger")) {
        if (ctx->conf->rank == 0) {
            for (i = 0; i < ctx->conf->rank; i++) {
                if (ctx->live[i] != -1)
                    ctx->live[i]++;
                if (ctx->live[i] > MISSED_TRIGGER_ALLOW) {
                    cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0,
                                  "event.live.down.%d", i);
                    ctx->live[i] = -1;
                } 
            }
            if (ctx->live[ctx->conf->rank] == -1) {
                cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0,
                              "event.live.up.%d", ctx->conf->rank);
                ctx->live[ctx->conf->rank] = 0;
            }
        } else {
            cmb_msg_send (ctx->zs_out_tree, NULL, NULL, 0,
                          "live.up.%d", ctx->conf->rank);
        }
    } else if (!strncmp (tag, live_up, strlen (live_up))) {
        int rank = strtoul (tag + strlen (live_up), NULL, 10);
        if (rank < 0 || rank >= ctx->conf->size)
            goto done;
        if (ctx->conf->rank == 0) {
            if (ctx->live[rank] == -1)
                cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0,
                              "event.live.up.%d", rank);
            ctx->live[rank] = 0;
        } else {
            cmb_msg_send (ctx->zs_out_tree, NULL, NULL, 0, "live.up.%d", rank);
        }
    }
done:
    if (tag)
        free (tag);
    return !shutdown;
}

static void *_thread (void *arg)
{
    zmq_pollitem_t zpa[] = {
       { .socket = ctx->zs_in, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    long tmout = -1;

    for (;;) {
        _zmq_poll(zpa, 1, tmout);

        if (zpa[0].revents & ZMQ_POLLIN) {
            if (!_readmsg ())
                break;
        }
    }
    return NULL;
}


void livesrv_init (conf_t *conf, void *zctx)
{
    int err;
    int i;

    ctx = xzmalloc (sizeof (struct ctx_struct));

    if (conf->rank == 0) {
        ctx->live = xzmalloc (conf->size * sizeof (int));
        for (i = 0; i < conf->size; i++)
            ctx->live[i] = -1;
    }
    ctx->conf = conf;

    ctx->zs_out_tree = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_tree, conf->plin_tree_uri);

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe (ctx->zs_in, "event.cmb.shutdown");
    _zmq_subscribe (ctx->zs_in, "event.sched.trigger");
    _zmq_subscribe (ctx->zs_in, "live.up.");

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
    _zmq_close (ctx->zs_out);
    _zmq_close (ctx->zs_out_event);
    _zmq_close (ctx->zs_out_tree);

    if (ctx->live)
        free (ctx->live);
    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
