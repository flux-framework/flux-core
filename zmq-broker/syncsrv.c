/* syncsrv.c - generate scheduling trigger */

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

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    void *zs_in;
    void *zs_out;
    void *zs_out_event;
    pthread_t t;
    conf_t *conf;
};

static ctx_t ctx = NULL;

static bool _poll (void)
{
    bool shutdown = false;
    zmq_mpart_t msg;
    zmq_pollitem_t zpa[] = {
       { .socket = ctx->zs_in, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    long tmout = ctx->conf->syncperiod_msec;

    _zmq_poll(zpa, 1, tmout);

    if (zpa[0].revents & ZMQ_POLLIN) {
        _zmq_mpart_init (&msg);
        _zmq_mpart_recv (&msg, ctx->zs_in, 0);
        if (cmb_msg_match (&msg, "event.cmb.shutdown"))
            shutdown = true;
        _zmq_mpart_close (&msg);
    } else { /* timeout */
        cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, "event.sched.trigger");
    }
    return !shutdown;
}

static void *_thread (void *arg)
{
    while (_poll ())
        ;
    return NULL;
}

void syncsrv_init (conf_t *conf, void *zctx)
{
    int err;

    ctx = xzmalloc (sizeof (struct ctx_struct));
    ctx->conf = conf;

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe (ctx->zs_in, "event.cmb.shutdown");

    err = pthread_create (&ctx->t, NULL, _thread, NULL);
    if (err) {
        fprintf (stderr, "syncsrv_init: pthread_create: %s\n", strerror (err));
        exit (1);
    }
}

void syncsrv_fini (conf_t *conf)
{
    int err;

    err = pthread_join (ctx->t, NULL);
    if (err) {
        fprintf (stderr, "syncsrv_fini: pthread_join: %s\n", strerror (err));
        exit (1);
    }
    _zmq_close (ctx->zs_in);
    _zmq_close (ctx->zs_out_event);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
