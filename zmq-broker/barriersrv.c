/* barriersrv.c - implement barriers */ 

/* FIXME: track clients and abort barrier on premature disconnect
 */

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
#include <czmq.h>
#include <json/json.h>

#include "zmq.h"
#include "cmb.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "barriersrv.h"

typedef struct _barrier_struct {
    char *name;
    char *exit_event;
    int nprocs;
    int count;
    zhash_t *clients;
    plugin_ctx_t *p;
} barrier_t;

typedef struct {
    zhash_t *barriers;
} ctx_t;

static void _barrier_destroy (void *arg)
{
    barrier_t *b = arg;
    plugin_ctx_t *p = b->p;

    if (b->name)
        free (b->name);
    if (b->exit_event) {
        zsocket_set_unsubscribe (p->zs_in_event, b->exit_event);
        free (b->exit_event);
    }
    free (b);
    return;
}

static barrier_t *_barrier_create (plugin_ctx_t *p, char *name, int nprocs)
{
    ctx_t *ctx = p->ctx;
    barrier_t *b;

    b = xzmalloc (sizeof (barrier_t));
    b->name = xstrdup (name);
    b->nprocs = nprocs;
    b->p = p;

    if (asprintf (&b->exit_event, "event.barrier.exit.%s", name) < 0)
        oom ();
    zsocket_set_subscribe (p->zs_in_event, b->exit_event);

    zhash_insert (ctx->barriers, b->name, b);
    zhash_freefn (ctx->barriers, b->name, _barrier_destroy);
    return b;
}

static int _barrier_enter_request (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    plugin_ctx_t *p = arg;
    json_object *no, *o = NULL;

    if (b->count == 0)
        return 0;
    if (!(o = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_int (b->count)))
        oom ();
    json_object_object_add (o, "count", no);
    if (!(no = json_object_new_int (b->nprocs)))
        oom ();
    json_object_object_add (o, "nprocs", no);
    /* will route to parent's barrier plugin */
    cmb_msg_send_rt (p->zs_req, o, "barrier.enter.%s", b->name);
    b->count = 0;
    if (o)
        json_object_put (o);
    return 0;
}

static void _barrier_enter (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    barrier_t *b;
    json_object *o = NULL, *count, *nprocs;
    char *sender = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &o, NULL, NULL) < 0) {
        err ("%s: error decoding message", __FUNCTION__);
        goto done;
    }
    if (o == NULL || !(sender = cmb_msg_sender (*zmsg))
                  || !(count = json_object_object_get (o, "count"))
                  || !(nprocs = json_object_object_get (o, "nprocs"))) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }

    b = zhash_lookup (ctx->barriers, name);
    if (!b)
        b = _barrier_create (p, name, json_object_get_int (nprocs));
    b->count += json_object_get_int (count);

    if (b->count == b->nprocs)
        cmb_msg_send (p->zs_out_event, NULL, "%s", b->exit_event);
    else if (p->conf->treeout_uri && p->timeout == -1)
        p->timeout = 1; /* 1 ms - then send count upstream */
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

static void _barrier_exit (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;

    zhash_delete (ctx->barriers, name);
    zmsg_destroy (zmsg);
}

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *name = NULL;

    if (cmb_msg_match_substr (*zmsg, "barrier.enter.", &name))
        _barrier_enter (p, name, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.barrier.exit.", &name))
        _barrier_exit (p, name, zmsg);

    if (name)
        free (name);
}

static void _timeout (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    assert (p->conf->treeout_uri != NULL);

    zhash_foreach (ctx->barriers, _barrier_enter_request, p);
    p->timeout = -1; /* disable timeout */
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    zsocket_set_subscribe (p->zs_in_event, "event.barrier.exit.");
    ctx->barriers = zhash_new ();
    p->timeout = -1; /* no timeout initially */
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zhash_destroy (&ctx->barriers);
    free (ctx);
}

struct plugin_struct barriersrv = {
    .name      = "barrier",
    .initFn    = _init,
    .finiFn    = _fini,
    .recvFn    = _recv,
    .timeoutFn = _timeout,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
