/* barriersrv.c - implement barriers */ 

/* FIXME: event.barrier.exit.<name> should be able to return error in JSON.
 * Send this if barrier entry specifies a known name with different nprocs.
 * Also: track local client uuid's who have entered barrier, and subscribe
 * to their disconnect messages.  Send an error on premature disconnect.
 * Idea: send this to out_tree instead of out_event and have the root
 * barriersrv relay it (once) to out_event to avoid storm on mass-disconnect.
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
#include "barriersrv.h"
#include "util.h"
#include "log.h"

typedef struct _barrier_struct {
    char *name;
    char *exit_tag;
    int nprocs;
    int count;
    struct timeval ctime;
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
    if (b->exit_tag) {
        zsocket_set_unsubscribe (p->zs_in_event, b->exit_tag);
        free (b->exit_tag);
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
    if (asprintf (&b->exit_tag, "event.barrier.exit.%s", name) < 0)
        oom ();
    xgettimeofday (&b->ctime, NULL);
    b->p = p;
    zsocket_set_subscribe (p->zs_in_event, b->exit_tag);
    zhash_insert (ctx->barriers, b->name, b);
    zhash_freefn (ctx->barriers, b->name, _barrier_destroy);
    return b;
}

static int _send_barrier_enter (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    plugin_ctx_t *p = arg;
    json_object *no, *o = NULL;

    if (b->count > 0) {
        assert (p->zs_out_tree != NULL);   
        if (!(o = json_object_new_object ()))
            oom ();
        if (!(no = json_object_new_int (b->count)))
            oom ();
        json_object_object_add (o, "count", no);
        if (!(no = json_object_new_int (b->nprocs)))
            oom ();
        json_object_object_add (o, "nprocs", no);
        cmb_msg_send_long (p->zs_out_tree, o, NULL, 0, 0, "barrier.enter.%s",
                           b->name);
        json_object_put (o);
        b->count = 0;
    }
    return 0;
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

static int _readmsg (plugin_ctx_t *p, void *socket)
{
    ctx_t *ctx = p->ctx;
    char *barrier_enter = "barrier.enter.";
    char *barrier_exit = "event.barrier.exit.";
    char *tag = NULL;
    json_object *o;

    if (cmb_msg_recv (socket, &tag, &o, NULL, NULL, ZMQ_DONTWAIT) < 0) {
        if (errno != EAGAIN)
            err ("cmb_msg_recv");
        return -1;
    }

    /* event.barrier.exit.<name> */
    if (!strncmp (tag, barrier_exit, strlen (barrier_exit))) {
        zhash_delete (ctx->barriers, tag + strlen (barrier_exit));

    /* barrier.enter.<name> */
    } else if (!strncmp (tag, barrier_enter, strlen (barrier_enter))) {
        char *name = tag + strlen (barrier_enter);
        barrier_t *b;
        int count, nprocs;

        if (_parse_barrier_enter (o, &count, &nprocs) < 0) {
            msg ("error parsing %s", tag);
            goto done;
        }
        b = zhash_lookup (ctx->barriers, name);
        if (!b)
            b = _barrier_create (p, name, nprocs);
        b->count += count;
        if (b->count == b->nprocs) /* destroy when we receive our own msg */
            cmb_msg_send (p->zs_out_event, "%s", b->exit_tag);
    }
done:
    if (tag)
        free (tag);
    if (o)
        json_object_put (o);
    return 0;
}

static void _poll (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zmq_pollitem_t zpa[] = {
{ .socket = p->zs_in,       .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
{ .socket = p->zs_in_event, .events = ZMQ_POLLIN, .revents = 0, .fd = -1 },
    };

    for (;;) {
        _zmq_poll(zpa, 2, -1);

        if (zpa[0].revents & ZMQ_POLLIN) {
            while (_readmsg (p, p->zs_in) != -1)
                ;
            /* As many entry messages as can be read in one go (above)
             * will be aggregated into a message sent upstream.
             */
            if (p->zs_out_tree)
                zhash_foreach (ctx->barriers, _send_barrier_enter, p);
        }
        if (zpa[1].revents & ZMQ_POLLIN) {
            while (_readmsg (p, p->zs_in_event) != -1)
                ;
        }
    }
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    zsocket_set_subscribe (p->zs_in, "barrier.enter.");
    zsocket_set_subscribe (p->zs_in_event, "event.barrier.exit.");
    ctx->barriers = zhash_new ();
}

static void _fini (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;
    zhash_destroy (&ctx->barriers);
    free (ctx);
}

struct plugin_struct barriersrv = {
    .initFn = _init,
    .finiFn = _fini,
    .pollFn = _poll,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
