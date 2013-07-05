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

#include "zmq.h"
#include "cmb.h"
#include "route.h"
#include "cmbd.h"
#include "plugin.h"
#include "util.h"
#include "log.h"

#include "barriersrv.h"

const int barrier_reduction_timeout_msec = 1;

typedef struct _barrier_struct {
    char *name;
    int nprocs;
    int count;
    zhash_t *clients;
    plugin_ctx_t *p;
    int errnum;
} barrier_t;

typedef struct {
    zhash_t *barriers;
} ctx_t;

static void _barrier_destroy (void *arg)
{
    barrier_t *b = arg;
    plugin_ctx_t *p = b->p;

    plugin_log (p, CMB_LOG_DEBUG,
                "destroy %s nprocs %d count %d errnum %d clients %d",
                b->name, b->nprocs, b->count, b->errnum,
                zhash_size (b->clients));
    zhash_destroy (&b->clients);
    free (b->name);
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
    if (!(b->clients = zhash_new ()))
        oom ();
    b->p = p;
    zhash_insert (ctx->barriers, b->name, b);
    zhash_freefn (ctx->barriers, b->name, _barrier_destroy);
    plugin_log (p, CMB_LOG_DEBUG, "create %s nprocs %d", name, nprocs);

    return b;
}

static void _free_zmsg (zmsg_t *zmsg)
{
    zmsg_destroy (&zmsg);
}

static int _barrier_add_client (barrier_t *b, char *sender, zmsg_t **zmsg)
{
    if (zhash_insert (b->clients, sender, *zmsg) < 0)
        return -1;
    zhash_freefn (b->clients, sender, (zhash_free_fn *)_free_zmsg);
    *zmsg = NULL; /* list owns it now */
    return 0;
}

static void _send_enter_request (plugin_ctx_t *p, barrier_t *b)
{
    json_object *o = util_json_object_new_object ();

    util_json_object_add_int (o, "count", b->count);
    util_json_object_add_int (o, "nprocs", b->nprocs);
    plugin_send_request (p, o, "barrier.enter.%s", b->name);
    json_object_put (o);
}

/* We have held onto our count long enough.  Send it upstream.
 */

static int _timeout_reduction (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    plugin_ctx_t *p = arg;

    if (b->count > 0) {
        _send_enter_request (p, b);
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

static void _barrier_enter (plugin_ctx_t *p, char *name, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    barrier_t *b;
    json_object *o = NULL;
    char *sender = NULL;
    int count, nprocs;

    if (cmb_msg_decode (*zmsg, NULL, &o) < 0 || o == NULL
     || !(sender = cmb_msg_sender (*zmsg))
     || util_json_object_get_int (o, "count", &count) < 0
     || util_json_object_get_int (o, "nprocs", &nprocs) < 0) {
        err ("%s: protocol error", __FUNCTION__);
        goto done;
    }


    if (!(b = zhash_lookup (ctx->barriers, name)))
        b = _barrier_create (p, name, nprocs);

    /* Distinguish client (tracked) vs downstream barrier plugin (untracked).
     * N.B. client, distinguished by sender uuid, can only enter barrier once.
     */
    if (strcmp (sender, "barrier") != 0) {
        if (_barrier_add_client (b, sender, zmsg) < 0) {
            plugin_send_response_errnum (p, zmsg, EEXIST);
            plugin_log (p, CMB_LOG_ERR,
                        "abort %s due to double entry by client %s",
                        name, sender);
            plugin_send_event (p, "event.barrier.abort.%s", b->name);
            goto done;
        }
    }

    /* If the count has been reached, terminate the barrier;
     * o/w set timer to pass count upstream and zero it here.
     */
    b->count += count;
    if (b->count == b->nprocs)
        plugin_send_event (p, "event.barrier.exit.%s", b->name);
    else if (!plugin_treeroot (p) && !plugin_timeout_isset (p))
        plugin_timeout_set (p, barrier_reduction_timeout_msec);
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

static int _disconnect (const char *key, void *item, void *arg)
{
    barrier_t *b = item;
    char *sender = arg;

    if (zhash_lookup (b->clients, sender)) {
        plugin_log (b->p, CMB_LOG_ERR,
                    "abort %s due to premature disconnect by client %s",
                    b->name, sender);
        plugin_send_event (b->p, "event.barrier.abort.%s", b->name);
    }
    return 0;
}

static void _barrier_disconnect (plugin_ctx_t *p, zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    char *sender = cmb_msg_sender (*zmsg);

    if (sender) {
        zhash_foreach (ctx->barriers, _disconnect, sender);
        free (sender);
    }
    zmsg_destroy (zmsg);
}

/* Upon barrier termination, notify any "connected" clients.
 */

static int _send_enter_response (const char *key, void *item, void *arg)
{
    zmsg_t *zmsg = item;
    barrier_t *b = arg;
    zmsg_t *cpy;

    if (!(cpy = zmsg_dup (zmsg)))
        oom ();
    plugin_send_response_errnum (b->p, &cpy, b->errnum);
    return 0;
}

static void _barrier_exit (plugin_ctx_t *p, char *name, int errnum,
                           zmsg_t **zmsg)
{
    ctx_t *ctx = p->ctx;
    barrier_t *b;

    if ((b = zhash_lookup (ctx->barriers, name))) {
        b->errnum = errnum;       
        zhash_foreach (b->clients, _send_enter_response, b);
        zhash_delete (ctx->barriers, name);
    }
    zmsg_destroy (zmsg);
}

/* Define plugin entry points.
 */

static void _recv (plugin_ctx_t *p, zmsg_t **zmsg, zmsg_type_t type)
{
    char *name = NULL;

    if (cmb_msg_match_substr (*zmsg, "barrier.enter.", &name))
        _barrier_enter (p, name, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.barrier.exit.", &name))
        _barrier_exit (p, name, 0, zmsg);
    else if (cmb_msg_match_substr (*zmsg, "event.barrier.abort.", &name))
        _barrier_exit (p, name, ECONNABORTED, zmsg);
    else if (cmb_msg_match (*zmsg, "barrier.disconnect"))
        _barrier_disconnect (p, zmsg);

    if (name)
        free (name);
}

static void _timeout (plugin_ctx_t *p)
{
    ctx_t *ctx = p->ctx;

    assert (!plugin_treeroot (p));

    zhash_foreach (ctx->barriers, _timeout_reduction , p);
    plugin_timeout_clear (p);
}

static void _init (plugin_ctx_t *p)
{
    ctx_t *ctx;

    ctx = p->ctx = xzmalloc (sizeof (ctx_t));
    zsocket_set_subscribe (p->zs_evin, "event.barrier.");
    ctx->barriers = zhash_new ();
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
