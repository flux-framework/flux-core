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
    void *zs_out_event;
    pthread_t t;
    conf_t *conf;
};

static ctx_t ctx = NULL;

static void *_thread (void *arg)
{
    for (;;) {
        usleep (ctx->conf->syncperiod_msec * 1000);
        cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, 0,
                      "event.sched.trigger");
    }
    return NULL;
}

void syncsrv_init (conf_t *conf, void *zctx)
{
    int err;

    ctx = xzmalloc (sizeof (struct ctx_struct));
    ctx->conf = conf;

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

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
    _zmq_close (ctx->zs_out_event);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
