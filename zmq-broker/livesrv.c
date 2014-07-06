/* livesrv.c - node liveness service */

/* This module builds on the following services in the cmbd:
 * cmb.peers - get idle time (in heartbeats) for non-module peers
 * cmb.failover , cmb.recover - switch parent
 * The cmbd expects failover/recovery to be driven externally (e.g. by us).
 * The cmbd maintains a hash of peers and their idle time, and also sends
 * a cmb.ping upstream on the heartbeat if nothing else has been sent in the
 * previous epoch, as a keep-alive.  So if the idle time for a child is > 1,
 * something is probably wrong.
 *
 * In this module, parents monitor their children on the heartbeat.  That is,
 * we call cmb.peers (locally) and check the idle time of our children.
 * If a child changes state, we publish a live.cstate event, intended to
 * reach grandchildren so they can find a better parent without relying on
 * upstream services which would be unavailable to them for the moment.
 *
 * Monitoring does not begin until children check in the first time
 * with a live.hello.  Parents discover their children via the live.hello
 * request, and children discover their (grand-)parents via the response.
 *
 * We listen for live.cstate events involving our (grand-)parents.
 * If our current parent goes down, we failover to a new one.
 * If our primary parent comes back, we recover (fail back to it).
 *
 * Notes:
 * 1) Since montoring begins only after a good connection is established,
 * this module won't detect nodes that die during initial wireup.
 * 2) If a live.cstate change to CS_FAIL is received for _me_, drop all
 * children.
 * 3) Befoere failover/recovery, send a live.goodbye to tell the old parent
 * to forget about the child.  #2 and #3 are unlikely to be helpful if parent
 * is really dead, but if it's not quite dead...
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
#include <sys/time.h>
#include <ctype.h>
#include <zmq.h>
#include <czmq.h>
#include <json/json.h>

#include "zmsg.h"
#include "util.h"
#include "log.h"
#include "plugin.h"
#include "shortjson.h"

typedef enum { CS_OK, CS_SLOW, CS_FAIL } cstate_t;

typedef struct {
    int rank;
    char *uri;
    cstate_t state;
    bool cur;
    bool pri;
} parent_t;

typedef struct {
    int rank;
    char *rankstr;
    cstate_t state;
} child_t;

typedef struct {
    int max_idle;
    int slow;
    int epoch;
    int rank;
    bool master;
    zlist_t *parents;
    zhash_t *children;
    cstate_t mystate;
    flux_t h;
} ctx_t;

static void parent_destroy (parent_t *p);
static void child_destroy (child_t *c);
static int hello (ctx_t *ctx);
static int goodbye (ctx_t *ctx, int parent_rank);

static const int default_max_idle = 5;
static const int default_slow = 3;

static void freectx (ctx_t *ctx)
{
    parent_t *p;

    while ((p = zlist_pop (ctx->parents)))
        parent_destroy (p);
    zlist_destroy (&ctx->parents);
    zhash_destroy (&ctx->children);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "livesrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->max_idle = default_max_idle;
        ctx->slow = default_slow;
        ctx->rank = flux_rank (h);
        ctx->master = flux_treeroot (h);
        if (!(ctx->parents = zlist_new ()))
            oom ();
        if (!(ctx->children = zhash_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "livesrv", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

static void child_destroy (child_t *c)
{
    free (c->rankstr);
    free (c);
}

static child_t *child_create (int rank)
{
    child_t *c = xzmalloc (sizeof (*c));
    c->rank = rank;
    if (asprintf (&c->rankstr, "%d", rank) < 0)
        oom ();
    return c;
}

static void parent_destroy (parent_t *p)
{
    free (p->uri);
    free (p);
}

static parent_t *parent_create (int rank, const char *uri)
{
    parent_t *p = xzmalloc (sizeof (*p));
    p->rank = rank;
    p->uri = xstrdup (uri);
    return p;
}

static parent_t *parent_fromjson (JSON o)
{
    int rank;
    const char *uri;
    if (!Jget_int (o, "rank", &rank) || !Jget_str (o, "uri", &uri))
        return NULL;
    return parent_create (rank, uri);
}

static parent_t *parent_fromctx (ctx_t *ctx)
{
    char *uri;
    parent_t *p;

    if (!(uri = flux_getattr (ctx->h, -1, "cmbd-request-uri")))
        return NULL;
    p = parent_create (ctx->rank, uri);
    free (uri);
    return p;
}

static JSON parent_tojson (parent_t *p)
{
    JSON o = Jnew ();

    Jadd_int (o, "rank", p->rank);
    Jadd_str (o, "uri", p->uri);
    return o;
}

static JSON parents_tojson (ctx_t *ctx)
{
    parent_t *p;
    JSON el;
    JSON ar = Jnew_ar ();

    p = zlist_first (ctx->parents);
    while (p) {
        el = parent_tojson (p);
        Jadd_ar_obj (ar, el);
        Jput (el);
        p = zlist_next (ctx->parents);
    }
    return ar;
}

static void parents_fromjson (ctx_t *ctx, JSON ar)
{
    int i, len;
    JSON el;
    parent_t *p;

    if (Jget_ar_len (ar, &len)) {
        for (i = 0; i < len; i++) {
            if (Jget_ar_obj (ar, i, &el) && (p = parent_fromjson (el)))
                zlist_append (ctx->parents, p);
        }
    }
}

static void recover (ctx_t *ctx)
{
    parent_t *new = zlist_first (ctx->parents);
    parent_t *old;

    assert (new != NULL);
    assert (new->state == CS_OK);
    assert (new->pri == true);

    old = zlist_next (ctx->parents);
    while (old) {
        if (old->cur == true)
            break;
        old = zlist_next (ctx->parents);
    }
    assert (old != NULL);
    old->cur = false;
    new->cur = true;
    goodbye (ctx, old->rank);
    if (flux_recover (ctx->h, -1) < 0)
        flux_log (ctx->h, LOG_ERR, "%s: %s", __FUNCTION__, strerror (errno));
    hello (ctx);
}

static void failover (ctx_t *ctx)
{
    parent_t *new;
    parent_t *old;

    new = zlist_first (ctx->parents);
    while (new) {
        if (new->state == CS_OK)
            break;
        new = zlist_next (ctx->parents);
    }
    if (!new)           /* no failover options */
        return;
    if (new->pri) {     /* recovery appropriate here */
        recover (ctx);
        return;
    }
    old = zlist_first (ctx->parents);
    while (old) {
        if (old->cur == true)
            break;
        old = zlist_next (ctx->parents);
    }
    assert (old != NULL);
    if (new) {
        old->cur = false;
        new->cur = true;
        goodbye (ctx, old->rank);
        if (flux_failover (ctx->h, -1, new->uri) < 0)
            flux_log (ctx->h, LOG_ERR, "%s: %s", __FUNCTION__,strerror (errno));
        hello (ctx);
    }
}

static int cstate_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON event = NULL;
    int epoch, parent, rank;
    cstate_t ostate, nstate;
    parent_t *p;
    int rc = 0;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
            || !Jget_int (event, "epoch", &epoch)
            || !Jget_int (event, "parent", &parent)
            || !Jget_int (event, "rank", &rank)
            || !Jget_int (event, "ostate", (int *)&ostate)
            || !Jget_int (event, "nstate", (int *)&nstate)) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (rank == ctx->rank) {
        ctx->mystate = nstate;
        goto done;
    }
    p = zlist_first (ctx->parents);
    while (p) {
        if (p->rank == rank) {
            p->state = nstate;
            if (p->cur && p->state == CS_FAIL)
                failover (ctx);
            else if (!p->cur && p->pri && p->state == CS_OK)
                recover (ctx);
            break;
        }
        p = zlist_next (ctx->parents);
    }
done:
    Jput (event);
    return rc;
}

static void cstate_change (ctx_t *ctx, child_t *c, cstate_t newstate)
{
    JSON event = Jnew ();

    Jadd_int (event, "rank", c->rank);
    Jadd_int (event, "ostate", c->state);
    c->state = newstate;
    Jadd_int (event, "nstate", c->state);
    Jadd_int (event, "parent", ctx->rank);
    Jadd_int (event, "epoch", ctx->epoch);
    flux_event_send (ctx->h, event, "live.cstate");
    Jput (event);
}

/* On each heartbeat, check idle for downstream peers.
 * Note: lspeer returns a JSON object indexed by peer socket id.
 * The socket id is the stringified rank for cmbds.
 */
static int hb_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON event = NULL;
    JSON peers = NULL;
    zlist_t *keys = NULL;
    char *key;

    if (cmb_msg_decode (*zmsg, NULL, &event) < 0 || event == NULL
            || !Jget_int (event, "epoch", &ctx->epoch)) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    /* If we are alive enough to receive a live.cstate change to CS_FAIL
     * for ourselves, drop all child monitoring state and unsubscribe to
     * the heartbeat.  Children will say hello again if they recover.
     */
    if (ctx->mystate == CS_FAIL) {
        zhash_destroy (&ctx->children);
        if (!(ctx->children = zhash_new ()))
            oom ();
        (void)flux_event_unsubscribe (h, "hb");
        goto done;
    }
    /* Check each child's idle state and publish live.cstate events
     * if there are any changes.
     */
    if (!(peers = flux_lspeer (h, -1))) {
        flux_log (h, LOG_ERR, "flux_lspeer: %s", strerror (errno));
        goto done;
    }
    if (!(keys = zhash_keys (ctx->children)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        JSON co;
        int idle = ctx->epoch;
        child_t *c = zhash_lookup (ctx->children, key);
        assert (c != NULL);
        if (Jget_obj (peers, c->rankstr, &co))
            Jget_int (co, "idle", &idle);
        switch (c->state) {
            case CS_OK:
                if (idle > ctx->max_idle)
                    cstate_change (ctx, c, CS_FAIL);
                else if (idle > ctx->slow)
                    cstate_change (ctx, c, CS_SLOW);
                break;
            case CS_SLOW:
                if (idle <= ctx->slow)
                    cstate_change (ctx, c, CS_OK);
                else if (idle > ctx->max_idle)
                    cstate_change (ctx, c, CS_FAIL);
                break;
            case CS_FAIL:
                if (idle <= ctx->slow)
                    cstate_change (ctx, c, CS_OK);
                else if (idle <= ctx->max_idle)
                    cstate_change (ctx, c, CS_SLOW);
                break;
        }
        key = zlist_next (keys);
    }
done:
    if (keys)
        zlist_destroy (&keys);
    Jput (event);
    Jput (peers);
    if (*zmsg)
        zmsg_destroy (zmsg);
    return 0;
}

static void max_idle_cb (const char *key, int val, void *arg, int errnum)
{
    ctx_t *ctx = arg;

    if (errnum != ENOENT && errnum != 0)
        return;
    if (errnum == ENOENT)
        val = default_max_idle;
    ctx->max_idle = val;
}

static int goodbye_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    int rank, prank;
    char *rankstr = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                            || !Jget_int (request, "parent-rank", &prank)
                            || !Jget_int (request, "rank", &rank)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (prank != ctx->rank) { /* in case misdirected to new parent */
        flux_respond_errnum (h, zmsg, EINVAL);
        goto done;
    }
    if (asprintf (&rankstr, "%d", rank) < 0)
        oom ();
    zhash_delete (ctx->children, rankstr);
    flux_respond_errnum (h, zmsg, 0);
done:
    if (rankstr)
        free (rankstr);
    Jput (request);
    return 0;
}

static int goodbye (ctx_t *ctx, int parent_rank)
{
    JSON request = Jnew ();
    int rc = -1;

    Jadd_int (request, "rank", ctx->rank);
    Jadd_int (request, "parent-rank", parent_rank);
    if (flux_request_send (ctx->h, request, "live.goodbye") < 0) {
        flux_log (ctx->h, LOG_ERR, "%s: flux_request_send %s", __FUNCTION__,
                  strerror (errno));
        goto done;
    }
    rc = 0;
done:
    Jput (request);
    return rc;
}

static void up_kvs (ctx_t *ctx, int rank)
{
    char *key;
    if (asprintf (&key, "conf.live.hello.%d", rank) < 0)
        oom ();
    (void)kvs_put_int (ctx->h, key, 1);
    (void)kvs_commit (ctx->h);
    free (key);
}

/* hello: parents discover their children, and children discover their
 * grandparents which are potential failover candidates.
 */
static int hello_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;
    JSON response = NULL;
    int rank;
    parent_t *p;
    child_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                            || !Jget_int (request, "rank", &rank)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    /* Subscribe to heartbeat event only when children are present.
     */
    if (zhash_size (ctx->children) == 0) {
        if (flux_event_subscribe (h, "hb") < 0) {
            flux_log (h, LOG_ERR, "flux_event_subscribe: %s", strerror (errno));
        }
    }
    /* Create a record for this child, unless already seen.
     */
    c = child_create (rank);
    if (zhash_insert (ctx->children, c->rankstr, c) < 0)
        child_destroy (c);
    else
        zhash_freefn (ctx->children, c->rankstr,(zhash_free_fn *)child_destroy);
    /* Note to kvs that child reported in.
     * FIXME: reduce
     */
    up_kvs (ctx, c->rank);

    if (kvs_watch_int (h, "conf.live.max-idle", max_idle_cb, ctx) < 0) {
        flux_log (h, LOG_ERR, "kvs_watch_int %s: %s", "conf.live.max-idle",
                  strerror (errno));
        return -1;
    }

    if ((p = parent_fromctx (ctx))) {   /* temporarily add "me" at pos 0 */
        if (zlist_push (ctx->parents, p) < 0)
            oom ();
    }
    response = parents_tojson (ctx);
    if (p) {                            /* remove me */
        zlist_pop (ctx->parents);
        parent_destroy (p);
    }
    flux_respond (h, zmsg, response);
done:
    Jput (request);
    Jput (response);
    return 0;
}

static int hello (ctx_t *ctx)
{
    JSON request = Jnew ();
    JSON response = NULL;
    parent_t *p;
    int rc;

    Jadd_int (request, "rank", ctx->rank);
    if (!(response = flux_rpc (ctx->h, request, "live.hello"))) {
        flux_log (ctx->h, LOG_ERR, "flux_rpc: %s", strerror (errno));
        goto done;
    }
    if (zlist_size (ctx->parents) == 0) {
        parents_fromjson (ctx, response);
        if ((p = zlist_first (ctx->parents)))
            p->cur = p->pri = true;
        if (zlist_size (ctx->parents) > 1) {
            if (flux_event_subscribe (ctx->h, "live.cstate") < 0) {
                flux_log (ctx->h, LOG_ERR, "flux_event_subscribe: %s",
                          strerror (errno));
            }
        }
    }
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,       "hb",                  hb_cb },
    { FLUX_MSGTYPE_EVENT,       "live.cstate",         cstate_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.hello",          hello_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.goodbye",        goodbye_request_cb },
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (ctx->master)
        up_kvs (ctx, ctx->rank);
    else if (hello (ctx) < 0)
        return -1;

    if (flux_msghandler_addvec (h, htab, htablen, ctx) < 0) {
        flux_log (h, LOG_ERR, "flux_msghandler_addvec: %s", strerror (errno));
        return -1;
    }
    if (flux_reactor_start (h) < 0) {
        flux_log (h, LOG_ERR, "flux_reactor_start: %s", strerror (errno));
        return -1;
    }
    return 0;
}

MOD_NAME ("live");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
