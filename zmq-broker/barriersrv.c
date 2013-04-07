/* barriersrv.c - implement barriers */ 

/* FIXME: handle disconnecting clients (send event.barrier.fail) */

/* FIXME: don't retire barrier names, keep them around to detect reuse */

#define _GNU_SOURCE
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
#include <sys/time.h>
#include <zmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "barriersrv.h"
#include "util.h"

typedef struct ctx_struct *ctx_t;

typedef struct _barrier_struct {
    char *name;
    char *exit_tag;
    int nprocs;
    int count;
    struct _barrier_struct *next;
    struct _barrier_struct *prev;
    struct timeval ctime;
} barrier_t;

struct ctx_struct {
    void *zs_in;
    void *zs_out;
    void *zs_out_event;
    void *zs_out_tree;
    barrier_t *barriers;
    pthread_t t;
    conf_t *conf;
};

/* after this long, report barrier count upstream (if not root) */
const struct timeval reduce_timeout = { .tv_sec = 0, .tv_usec = 2*1000 };

static ctx_t ctx = NULL;

static barrier_t *_barrier_create (char *name, int nprocs)
{
    barrier_t *b;

    b = xzmalloc (sizeof (barrier_t));
    b->name = xstrdup (name);
    b->nprocs = nprocs;
    if (asprintf (&b->exit_tag, "event.barrier.exit.%s", name) < 0)
        oom ();
    xgettimeofday (&b->ctime, NULL);
    _zmq_subscribe (ctx->zs_in, b->exit_tag);

    b->prev = NULL;
    b->next = ctx->barriers;
    if (b->next)
        b->next->prev = b;
    ctx->barriers = b;
    return b;
}

static void _barrier_destroy (barrier_t *b)
{
    if (b->name)
        free (b->name);
    if (b->exit_tag) {
        _zmq_unsubscribe (ctx->zs_in, b->exit_tag);
        free (b->exit_tag);
    }
    if (b->prev)
        b->prev->next = b->next;
    else
        ctx->barriers = b->next;
    if (b->next)
        b->next->prev = b->prev;
    free (b);
    return;
}

static barrier_t *_barrier_lookup (char *name)
{
    barrier_t *b;

    for (b = ctx->barriers; b != NULL; b = b->next) {
        if (!strcmp (name, b->name))
            break;
    }
    return b;
}

static void _send_barrier_enter (barrier_t *b)
{
    json_object *no, *o = NULL;

    assert (ctx->zs_out_tree != NULL);   
    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_int (b->count)))
        oom ();
    json_object_object_add (o, "count", no);
    if (!(no = json_object_new_int (b->nprocs)))
        oom ();
    json_object_object_add (o, "nprocs", no);
    cmb_msg_send (ctx->zs_out_tree, o, NULL, 0, "barrier.enter.%s", b->name);
    json_object_put (o);
}

static int _parse_barrier_enter (json_object *o, int *cp, int *np)
{
    json_object *count, *nprocs;

    count = json_object_object_get (o, "count"); 
    if (!count)
        goto error;
    nprocs = json_object_object_get (o, "nprocs"); 
    if (!nprocs)
        goto error;
    *cp = json_object_get_int (count);
    *np = json_object_get_int (nprocs);
    return 0;
error:
    return -1;
}

static bool _readmsg (void)
{
    char *barrier_enter = "barrier.enter.";
    char *barrier_exit = "event.barrier.exit.";
    bool shutdown = false;
    char *tag = NULL;
    json_object *o;

    if (cmb_msg_recv (ctx->zs_in, &tag, &o, NULL, 0) < 0) {
        fprintf (stderr, "cmb_msg_recv: %s\n", strerror (errno));
        goto done;
    }
    if (!strcmp (tag, "event.cmb.shutdown")) {
        shutdown = true;

    /* event.barrier.exit.<name> */
    } else if (!strncmp (tag, barrier_exit, strlen (barrier_exit))) {
        char *name = tag + strlen (barrier_exit);
        barrier_t *b = _barrier_lookup (name);

        if (b)
            _barrier_destroy (b);

    /* barrier.enter.<name> */
    } else if (!strncmp (tag, barrier_enter, strlen (barrier_enter))) {
        char *name = tag + strlen (barrier_enter);
        barrier_t *b;
        int count, nprocs;

        if (_parse_barrier_enter (o, &count, &nprocs) < 0){
            fprintf (stderr, "%s: parse error\n", tag);
            goto done;
        }
        b = _barrier_lookup (name);
        if (!b)
            b = _barrier_create (name, nprocs);
        b->count += count;
        if (b->count == b->nprocs) /* destroy when we receive our own msg */
            cmb_msg_send (ctx->zs_out_event, NULL, NULL, 0, b->exit_tag);
    }
done:
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);

    return ! shutdown;
}

static long _timeout (void)
{
    barrier_t *b;
    struct timeval now, t;
    long usec, tmout = -1;

    xgettimeofday (&now, NULL);
    for (b = ctx->barriers; b != NULL; b = b->next) {
        timersub (&now, &b->ctime, &t);
        if (b->count > 0 && timercmp (&t, &reduce_timeout, >)) {
            _send_barrier_enter (b);
            b->count = 0;
            b->ctime = now;
            timersub (&now, &b->ctime, &t);
        }
        usec = t.tv_sec*1000000L + t.tv_usec;
        if (tmout == -1 || usec < tmout)
            tmout = usec;
    }
    return tmout;
}

static void *_thread (void *arg)
{
    zmq_pollitem_t zpa[] = {
       { .socket = ctx->zs_in, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };
    long tmout = -1;

    for (;;) {
        if ((zmq_poll(zpa, 1, tmout)) < 0) {
            fprintf (stderr, "zmq_poll: %s\n", strerror (errno));
            exit (1);
        }
        if (zpa[0].revents & ZMQ_POLLIN)
            if (!_readmsg ())
                break;
        if (ctx->zs_out_tree)
            tmout = _timeout ();
    }
    return NULL;
}

void barriersrv_init (conf_t *conf, void *zctx)
{
    int err;

    ctx = xzmalloc (sizeof (struct ctx_struct));
    ctx->conf = conf;

    ctx->zs_out_event = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out_event, conf->plin_event_uri);

    if (!conf->root_server) {
        ctx->zs_out_tree = _zmq_socket (zctx, ZMQ_PUSH);
        _zmq_connect (ctx->zs_out_tree, conf->plin_tree_uri);
    }

    ctx->zs_out = _zmq_socket (zctx, ZMQ_PUSH);
    _zmq_connect (ctx->zs_out, conf->plin_uri);

    ctx->zs_in = _zmq_socket (zctx, ZMQ_SUB);
    _zmq_connect (ctx->zs_in, conf->plout_uri);
    _zmq_subscribe (ctx->zs_in, "barrier.");
    _zmq_subscribe (ctx->zs_in, "event.cmb.shutdown");

    err = pthread_create (&ctx->t, NULL, _thread, NULL);
    if (err) {
        fprintf (stderr, "barriersrv_init: pthread_create: %s\n", strerror (err));
        exit (1);
    }
}

void barriersrv_fini (void)
{
    int err;

    err = pthread_join (ctx->t, NULL);
    if (err) {
        fprintf (stderr, "barriersrv_fini: pthread_join: %s\n", strerror (err));
        exit (1);
    }
    _zmq_close (ctx->zs_in);
    _zmq_close (ctx->zs_out);
    _zmq_close (ctx->zs_out_event);
    if (ctx->zs_out_tree)
        _zmq_close (ctx->zs_out_tree);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
