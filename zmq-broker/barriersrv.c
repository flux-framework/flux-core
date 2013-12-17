/* barriersrv.c - implement barriers of arbitrary membership */ 

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

#include "zmsg.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

const int barrier_reduction_timeout_msec = 1;

typedef struct {
    zhash_t *barriers;
    flux_t h;
} ctx_t;

typedef struct _barrier_struct {
    char *name;
    int nprocs;
    int count;
    zhash_t *clients;
    ctx_t *ctx;
    int errnum;
} barrier_t;

static void freectx (ctx_t *ctx)
{
    zhash_destroy (&ctx->barriers);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "barriersrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->barriers = zhash_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "barriersrv", ctx, (FluxFreeFn)freectx);
    }

    return ctx;
}

static void barrier_destroy (void *arg)
{
    barrier_t *b = arg;

    flux_log (b->ctx->h, LOG_DEBUG,
              "destroy %s nprocs %d count %d errnum %d clients %d",
              b->name, b->nprocs, b->count, b->errnum,
              (int)zhash_size (b->clients));
    zhash_destroy (&b->clients);
    free (b->name);
    free (b);
    return;
}

static barrier_t *barrier_create (ctx_t *ctx, char *name, int nprocs)
{
    barrier_t *b;

    b = xzmalloc (sizeof (barrier_t));
    b->name = xstrdup (name);
    b->nprocs = nprocs;
    if (!(b->clients = zhash_new ()))
        oom ();
    b->ctx = ctx;
    zhash_insert (ctx->barriers, b->name, b);
    zhash_freefn (ctx->barriers, b->name, barrier_destroy);
    flux_log (ctx->h, LOG_DEBUG, "create %s nprocs %d", name, nprocs);

    return b;
}

static void free_zmsg (zmsg_t *zmsg)
{
    zmsg_destroy (&zmsg);
}

static int barrier_add_client (barrier_t *b, char *sender, zmsg_t **zmsg)
{
    if (zhash_insert (b->clients, sender, *zmsg) < 0)
        return -1;
    zhash_freefn (b->clients, sender, (zhash_free_fn *)free_zmsg);
    *zmsg = NULL; /* list owns it now */
    return 0;
}

static void send_enter_request (ctx_t *ctx, barrier_t *b)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "count", b->count);
    util_json_object_add_int (o, "nprocs", b->nprocs);
    util_json_object_add_int (o, "hopcount", 1);
    flux_request_send (ctx->h, o, "barrier.enter.%s", b->name);
    json_object_put (o);
}

/* We have held onto our count long enough.  Send it upstream.
 */

static int timeout_reduction (const char *key, void *item, void *arg)
{
    ctx_t *ctx = arg;
    barrier_t *b = item;

    if (b->count > 0) {
        send_enter_request (ctx, b);
        b->count = 0;
    }
    return 0;
}

/* Barrier entry happens in two ways:
 * - client calling cmb_barrier ()
 * - downstream barrier plugin sending count upstream.
 * In the first case only, we track client uuid to handle disconnect and
 * notification upon barrier termination.
 */

static void barrier_enter (ctx_t *ctx, char *name, zmsg_t **zmsg)
{
    barrier_t *b;
    json_object *o = NULL;
    char *sender = NULL;
    int count, nprocs, hopcount;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
     || !(sender = cmb_msg_sender (*zmsg))
     || util_json_object_get_int (o, "count", &count) < 0
     || util_json_object_get_int (o, "nprocs", &nprocs) < 0) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }

    if (!(b = zhash_lookup (ctx->barriers, name)))
        b = barrier_create (ctx, name, nprocs);

    /* Distinguish client (tracked) vs downstream barrier plugin (untracked).
     * A client, distinguished by hopcount > 0, can only enter barrier once.
     */
    if (util_json_object_get_int (o, "hopcount", &hopcount) < 0) {
        if (barrier_add_client (b, sender, zmsg) < 0) {
            flux_respond_errnum (ctx->h, zmsg, EEXIST);
            flux_log (ctx->h, LOG_ERR,
                        "abort %s due to double entry by client %s",
                        name, sender);
            if (flux_event_send (ctx->h, NULL, "event.barrier.abort.%s",
                                                                b->name) < 0)
                err_exit ("%s: flux_event_send", __FUNCTION__);
            goto done;
        }
    }

    /* If the count has been reached, terminate the barrier;
     * o/w set timer to pass count upstream and zero it here.
     */
    b->count += count;
    if (b->count == b->nprocs) {
        if (flux_event_send (ctx->h, NULL, "event.barrier.exit.%s",
                                                                b->name) < 0)
            err_exit ("%s: flux_event_send", __FUNCTION__);
    } else if (!flux_treeroot (ctx->h) && !flux_timeout_isset (ctx->h))
        flux_timeout_set (ctx->h, barrier_reduction_timeout_msec);
done:
    if (o)
        json_object_put (o);
    if (*zmsg)
        zmsg_destroy (zmsg);
    if (sender)
        free (sender);
}

/* Upon client disconnect, abort any pending barriers it was
 * participating in.
 */

static int disconnect (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    ctx_t *ctx = b->ctx;
    char *sender = arg;

    if (zhash_lookup (b->clients, sender)) {
        flux_log (ctx->h, LOG_ERR,
                    "abort %s due to premature disconnect by client %s",
                    b->name, sender);
        if (flux_event_send (ctx->h, NULL, "event.barrier.abort.%s",
                                                            b->name) < 0)
            err_exit ("%s: flux_event_send", __FUNCTION__);
    }
    return 0;
}

static void barrier_disconnect (ctx_t *ctx, zmsg_t **zmsg)
{
    char *sender = cmb_msg_sender (*zmsg);

    if (sender) {
        zhash_foreach (ctx->barriers, disconnect, sender);
        free (sender);
    }
    zmsg_destroy (zmsg);
}

/* Upon barrier termination, notify any "connected" clients.
 */

static int send_enter_response (const char *key, void *item, void *arg)
{
    zmsg_t *zmsg = item;
    barrier_t *b = arg;
    zmsg_t *cpy;

    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    flux_respond_errnum (b->ctx->h, &cpy, b->errnum);
    return 0;
}

static void barrier_exit (ctx_t *ctx, char *name, int errnum, zmsg_t **zmsg)
{
    barrier_t *b;

    if ((b = zhash_lookup (ctx->barriers, name))) {
        b->errnum = errnum;       
        zhash_foreach (b->clients, send_enter_response, b);
        zhash_delete (ctx->barriers, name);
    }
    zmsg_destroy (zmsg);
}

/* Define plugin entry points.
 */

static int barriersrv_recv (flux_t h, zmsg_t **zmsg, int typemask)
{
    ctx_t *ctx = getctx (h);
    char *name = NULL;

    if (cmb_msg_match_substr (*zmsg, "barrier.enter.", &name))
        barrier_enter (ctx, name, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.barrier.exit.", &name))
        barrier_exit (ctx, name, 0, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.barrier.abort.", &name))
        barrier_exit (ctx, name, ECONNABORTED, zmsg);
    else if (cmb_msg_match (*zmsg, "barrier.disconnect"))
        barrier_disconnect (ctx, zmsg);

    if (name)
        free (name);
    return 0;
}

static int timeout_cb (flux_t h, void *arg)
{
    ctx_t *ctx = arg;

    assert (!flux_treeroot (h));

    zhash_foreach (ctx->barriers, timeout_reduction, ctx);
    flux_timeout_set (h, 0);
    return 0;
}

static int barriersrv_init (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (flux_event_subscribe (h, "event.barrier.") < 0) {
        err ("%s: flux_event_subscribe", __FUNCTION__);
        return -1;
    }
    if (flux_tmouthandler_set (h, timeout_cb, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_tmouthandler_set: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

static void barriersrv_fini (flux_t h)
{
    //ctx_t *ctx = getctx (h);
    //
    if (flux_event_unsubscribe (h, "event.barrier.") < 0)
        err_exit ("%s: flux_event_subscribe", __FUNCTION__);
}

const struct plugin_ops ops = {
    .init    = barriersrv_init,
    .fini    = barriersrv_fini,
    .recv    = barriersrv_recv,
};

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
