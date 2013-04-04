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
#include <ctype.h>
#include <zmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "barriersrv.h"

typedef struct ctx_struct *ctx_t;

struct ctx_struct {
    void *zs_in;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    pthread_t t;
    conf_t *conf;
};

static ctx_t ctx = NULL;

static void _oom (void)
{
    fprintf (stderr, "out of memory\n");
    exit (1);
}

static void *_zmalloc (size_t size)
{
    void *new;

    new = malloc (size);
    if (!new)
        _oom ();
    memset (new, 0, size);
    return new;
}

static bool _poll (void)
{
    bool shutdown = false;
    zmq_2part_t msg;
    zmq_pollitem_t zpa[] = {
       { .socket = ctx->zs_in, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    long tmout = ctx->conf->syncperiod_msec * 1000; 

    if ((zmq_poll(zpa, 1, tmout)) < 0) {
        fprintf (stderr, "zmq_poll: %s\n", strerror (errno));
        exit (1);
    }
    if (zpa[0].revents & ZMQ_POLLIN) {
        _zmq_2part_init (&msg);
        _zmq_2part_recv (ctx->zs_in, &msg, 0);
        if (_zmq_2part_match (&msg, "event.cmb.shutdown"))
            shutdown = true;
        _zmq_2part_close (&msg);
    } else /* timeout */
        _zmq_2part_send_json (ctx->zs_out_event, NULL, "event.sched.trigger");
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

    ctx = _zmalloc (sizeof (struct ctx_struct));
    ctx->conf = conf;

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    if (conf->root_server)
        _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    ctx->zs_out_tree = _zmq_socket (zctx, ZMQ_PUSH);
    if (!conf->root_server)
        _zmq_connect (ctx->zs_out_tree, conf->plin_tree_uri);

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

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
    _zmq_close (ctx->zs_out);
    _zmq_close (ctx->zs_out_event);
    _zmq_close (ctx->zs_out_tree);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
