/* barriersrv.c - implement barriers */ 
/* FIXME: handle disconnecting clients (send event.barrier.fail) */

/* FIXME: don't retire barrier names, keep them around to detect reuse */

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

typedef struct _barrier_struct {
    char name[32];
    int maxcount;
    int counter;
    struct _barrier_struct *next;
    struct _barrier_struct *prev;
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

static barrier_t *_barrier_create (char *name, int maxcount)
{
    barrier_t *b;

    b = _zmalloc (sizeof (barrier_t));
    snprintf (b->name, sizeof (b->name), "%s", name);
    b->maxcount = maxcount;
    b->prev = NULL;
    b->next = ctx->barriers;
    if (b->next)
        b->next->prev = b;
    ctx->barriers = b;
    return b;
}

static void _barrier_destroy (barrier_t *b)
{
    if (b->prev)
        b->prev->next = b->next;
    else
        ctx->barriers = b->next;
    if (b->next)
        b->next->prev = b->prev;
    if (ctx->conf->root_server) {
        _zmq_2part_send_json (ctx->zs_out_event, NULL,
                              "event.barrier.exit.%s", b->name);
    } else {
        json_object *no, *o = NULL;
       
        if (!(o = json_object_new_object ()))
            _oom ();
        if (!(no = json_object_new_int (b->maxcount)))
            _oom ();
        json_object_object_add (o, "count", no);
        _zmq_2part_send_json (ctx->zs_out_tree, o, "barrier.enter.%s", b->name);
    }
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

static int _parse_barrier_enter (json_object *o, int *cp, int *np, int *tp)
{
    json_object *count, *nprocs, *tasks_per_node;

    count = json_object_object_get (o, "count"); 
    if (!count)
        goto error;
    nprocs = json_object_object_get (o, "nprocs"); 
    if (!nprocs)
        goto error;
    tasks_per_node = json_object_object_get (o, "tasks_per_node"); 
    if (!tasks_per_node)
        goto error;
    *cp = json_object_get_int (count);
    *np = json_object_get_int (nprocs);
    *tp = json_object_get_int (tasks_per_node);
    return 0;
error:
    return -1;
}

static void *_thread (void *arg)
{
    char *prefix = "barrier.enter.";
    int plen = strlen (prefix);
    bool shutdown = false;
    char *tag;
    json_object *o;

    while (!shutdown) {
        if (_zmq_2part_recv_json (ctx->zs_in, &tag, &o) < 0) {
            fprintf (stderr, "_zmq_2part_recv_json: %s\n", strerror (errno));
            continue;
        }
        if (!strcmp (tag, "event.cmb.shutdown")) {
            shutdown = true;
            goto next;
        }
        if (!strncmp (tag, prefix, plen)) { /* barrier.enter.<name> */
            char *name = tag + plen;
            barrier_t *b;
            int count, nprocs, tasks_per_node;

            if (_parse_barrier_enter (o, &count, &nprocs, &tasks_per_node) < 0){
                fprintf (stderr, "%s: parse error\n", tag);
                goto next;
            }
            b = _barrier_lookup (name);
            /* FIXME: this hardwires direct connect to root by all nodes */
            if (!b) { /* FIXME: support multi-level tree */
                int n = (ctx->conf->root_server ? nprocs : tasks_per_node);
                b = _barrier_create (name, n);
            }
            b->counter += count;
            if (b->counter == b->maxcount)
                _barrier_destroy (b);
        }
next:
        free (tag);
        if (o)
            json_object_put (o);
    }
    return NULL;
}

void barriersrv_init (conf_t *conf, void *zctx)
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
    _zmq_close (ctx->zs_out_tree);

    free (ctx);
    ctx = NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
