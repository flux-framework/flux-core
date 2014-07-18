/* livesrv.c - node liveness service */

/* This module builds on the following services in the cmbd:
 *   cmb.peers - get idle time (in heartbeats) for non-module peers
 *   cmb.failover - switch to new parent
 * The cmbd expects failover to be driven externally (e.g. by us).
 * The cmbd maintains a hash of peers and their idle time, and also sends
 * a cmb.ping upstream on the heartbeat if nothing else has been sent in the
 * previous epoch, as a keep-alive.  So if the idle time for a child is > 1,
 * something is probably wrong.
 *
 * In this module, parents monitor their children on the heartbeat.  That is,
 * we call cmb.peers (locally) and check the idle time of our children.
 * If a child changes state, we publish a live.cstate event, intended to
 * reach grandchildren so they can failover to new parent without relying on
 * upstream services which would be unavailable to them for the moment.
 *
 * Monitoring does not begin until children check in the first time
 * with a live.hello.  Parents discover their children via the live.hello
 * request, and children discover their (grand-)parents via the response.
 *
 * We listen for live.cstate events involving our (grand-)parents.
 * If our current parent goes down, we failover to a new one.
 * We do not attempt to restore the original toplogy - that would be
 * unnecessarily disruptive and should be done manually if at all.
 *
 * Notes:
 * 1) Since montoring begins only after a good connection is established,
 * this module won't detect nodes that die during initial wireup.
 * 2) If a live.cstate change to CS_FAIL is received for _me_, drop all
 * children.  Similarly a child that sends live.goodbye message causes
 * parent to forget about that child.
 */

/* Regarding conf.live.status:
 * It's created by the master (rank=0) node with the master "ok", and
 * all other nodes "unknown".  When a parent gets a hello from a child,
 * this is reported (through a reduction sieve) to the master, which
 * transitions those nodes to "ok".  Finally, the master listens for
 * live.cstate events and transitions nodes accordingly.
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
#include "reduce.h"
#include "hostlist.h"

typedef enum { CS_OK, CS_SLOW, CS_FAIL, CS_UNKNOWN } cstate_t;

typedef struct {
    hostlist_t ok;
    hostlist_t fail;
    hostlist_t slow;
    hostlist_t unknown;
} ns_t;

typedef struct {
    int rank;
    char *uri;
    cstate_t state;
} parent_t;

typedef struct {
    int rank;
    char *rankstr;
    cstate_t state;
} child_t;

typedef struct {
    int max_idle;
    int slow_idle;
    int epoch;
    int rank;
    char *rankstr;
    bool master;
    zlist_t *parents;   /* current parent is first in list */
    zhash_t *children;
    bool hb_subscribed;
    red_t r;
    ns_t *ns;           /* master only */
    JSON topo;          /* master only */
    flux_t h;
} ctx_t;

static void parent_destroy (parent_t *p);
static void child_destroy (child_t *c);
static int hello (ctx_t *ctx);
static int goodbye (ctx_t *ctx, int parent_rank);
static void manage_subscriptions (ctx_t *ctx);

static void hello_sink (flux_t h, void *item, int batchnum, void *arg);
static void hello_reduce (flux_t h, zlist_t *items, int batchnum, void *arg);

static const int default_max_idle = 5;
static const int default_slow_idle = 3;
static const int reduce_timeout_master_msec = 100;
static const int reduce_timeout_slave_msec = 10;

static void ns_chg_one (ctx_t *ctx, const char *s, cstate_t from, cstate_t to);
static int ns_sync (ctx_t *ctx);

static void freectx (ctx_t *ctx)
{
    parent_t *p;

    while ((p = zlist_pop (ctx->parents)))
        parent_destroy (p);
    zlist_destroy (&ctx->parents);
    zhash_destroy (&ctx->children);
    flux_red_destroy (ctx->r);
    Jput (ctx->topo);
    free (ctx->rankstr);
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = (ctx_t *)flux_aux_get (h, "livesrv");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->max_idle = default_max_idle;
        ctx->slow_idle = default_slow_idle;
        ctx->rank = flux_rank (h);
        if (asprintf (&ctx->rankstr, "%d", ctx->rank) < 0)
            oom ();
        ctx->master = flux_treeroot (h);
        if (!(ctx->parents = zlist_new ()))
            oom ();
        if (!(ctx->children = zhash_new ()))
            oom ();
        ctx->r = flux_red_create (h, hello_sink, ctx);
        flux_red_set_reduce_fn (ctx->r, hello_reduce);
        flux_red_set_flags (ctx->r, FLUX_RED_TIMEDFLUSH);
        if (ctx->master)
            flux_red_set_timeout_msec (ctx->r, reduce_timeout_master_msec);
        else
            flux_red_set_timeout_msec (ctx->r, reduce_timeout_slave_msec);
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
    if (p->uri)
        free (p->uri);
    free (p);
}

static parent_t *parent_create (int rank, const char *uri)
{
    parent_t *p = xzmalloc (sizeof (*p));
    p->rank = rank;
    if (uri)
        p->uri = xstrdup (uri);
    return p;
}

static parent_t *parent_fromjson (JSON o)
{
    int rank;
    const char *uri = NULL;
    if (!Jget_int (o, "rank", &rank))
        return NULL;
    (void)Jget_str (o, "uri", &uri); /* optional */
    return parent_create (rank, uri);
}

static JSON parent_tojson (parent_t *p)
{
    JSON o = Jnew ();

    Jadd_int (o, "rank", p->rank);
    if (p->uri)
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

/* Build ctx->parents from JSON array received in hello response.
 * Fix up first entry, which is the primary (and current) parent.
 * Set its URI here where we have access to one that is suitable for
 * zmq_connect(), as opposed to the parent which has a zmq_bind() URI
 * that could be a wildcard.
 */
static void parents_fromjson (ctx_t *ctx, JSON ar)
{
    int i, len;
    JSON el;
    parent_t *p;

    if (Jget_ar_len (ar, &len)) {
        for (i = 0; i < len; i++) {
            if (Jget_ar_obj (ar, i, &el) && (p = parent_fromjson (el))) {
                if (i == 0) {
                    if (p->uri) /* unlikely */
                        free (p->uri);
                    p->uri = flux_getattr (ctx->h, -1, "cmbd-parent-uri");
                }
                if (zlist_append (ctx->parents, p) < 0)
                    oom ();
            }
        }
    }
}

static int reparent (ctx_t *ctx, int oldrank, parent_t *p)
{
    int rc = -1;

    if (oldrank == p->rank) {
        rc = 0;
        goto done;
    }
    if (p->state == CS_FAIL) {
        errno = EINVAL;
        goto done;
    }
    zlist_remove (ctx->parents, p); /* move p to head of parents list */
    if (zlist_push (ctx->parents, p) < 0)
        oom ();
    goodbye (ctx, oldrank);
    if ((rc = flux_reparent (ctx->h, -1, p->uri)) < 0)
        flux_log (ctx->h, LOG_ERR, "%s %s: %s",
                  __FUNCTION__, p->uri, strerror (errno));
    hello (ctx);
done:
    return rc;
}

/* Reparent to next alternate parent.
 */
static int failover (ctx_t *ctx)
{
    parent_t *p;
    int oldrank;

    if ((p = zlist_first (ctx->parents))) {
        oldrank = p->rank;
        p = zlist_next (ctx->parents);
    }
    while (p && p->state == CS_FAIL)
        p = zlist_next (ctx->parents);
    if (!p) {
        errno = ESRCH;
        return -1;
    }
    return reparent (ctx, oldrank, p);
}

/* Reparent to original parent.
 */
static int recover (ctx_t *ctx)
{
    parent_t *p;
    int oldrank, maxrank = -1; /* max rank will be orig. parent */
    parent_t *newp = NULL;

    if ((p = zlist_first (ctx->parents)))
        oldrank = p->rank;
    while (p) {
        if (p->rank > maxrank) {
            maxrank = p->rank;
            newp = p;
        }
        p = zlist_next (ctx->parents);
    }
    if (!newp) {
        errno = ESRCH;
        return -1;
    }
    return reparent (ctx, oldrank, newp);
}

static int cstate_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON event = NULL;
    int epoch, parent, rank;
    cstate_t ostate, nstate;
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
        if (nstate == CS_FAIL) {        /* I'm dead - stop watching children */
            zhash_destroy (&ctx->children);
            if (!(ctx->children = zhash_new ()))
                oom ();
            manage_subscriptions (ctx);
        }
    } else {
        parent_t *p = zlist_first (ctx->parents);
        while (p) {
            if (p->rank == rank) {
                p->state = nstate;
                break;
            }
            p = zlist_next (ctx->parents);
        }
        if ((p = zlist_first (ctx->parents)) && p->state == CS_FAIL)
            if (failover (ctx) < 0)
                flux_log (h, LOG_ERR, "no failover candidates");
    }
    if (ctx->master) {
        char *rankstr;
        if (asprintf (&rankstr, "%d", rank) < 0)
            oom ();
        ns_chg_one (ctx, rankstr, ostate, nstate);
        if (ns_sync (ctx) < 0)
            flux_log (h, LOG_ERR, "%s: ns_sync: %s",
                      __FUNCTION__, strerror (errno));
        free (rankstr);
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
                else if (idle > ctx->slow_idle)
                    cstate_change (ctx, c, CS_SLOW);
                break;
            case CS_SLOW:
                if (idle <= ctx->slow_idle)
                    cstate_change (ctx, c, CS_OK);
                else if (idle > ctx->max_idle)
                    cstate_change (ctx, c, CS_FAIL);
                break;
            case CS_FAIL:
                if (idle <= ctx->slow_idle)
                    cstate_change (ctx, c, CS_OK);
                else if (idle <= ctx->max_idle)
                    cstate_change (ctx, c, CS_SLOW);
                break;
            case CS_UNKNOWN: /* can't happen */
                assert (c->state != CS_UNKNOWN);
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

static void manage_subscriptions (ctx_t *ctx)
{
    if (ctx->hb_subscribed && zhash_size (ctx->children) == 0) {
        if (flux_event_unsubscribe (ctx->h, "hb") < 0)
            flux_log (ctx->h, LOG_ERR, "%s: flux_event_unsubscribe hb: %s",
                      __FUNCTION__, strerror (errno));
        else
            ctx->hb_subscribed = false;
    } else if (!ctx->hb_subscribed && zhash_size (ctx->children) > 0) {
        if (flux_event_subscribe (ctx->h, "hb") < 0)
            flux_log (ctx->h, LOG_ERR, "%s: flux_event_subscribe hb: %s",
                      __FUNCTION__, strerror (errno));
        else
            ctx->hb_subscribed = true;
    }
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

static void slow_idle_cb (const char *key, int val, void *arg, int errnum)
{
    ctx_t *ctx = arg;

    if (errnum != ENOENT && errnum != 0)
        return;
    if (errnum == ENOENT)
        val = default_slow_idle;
    ctx->slow_idle = val;
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
    manage_subscriptions (ctx);
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

static char *hl_string (hostlist_t hl)
{
    int len = 64;
    char *s = xzmalloc (len);

    hostlist_sort (hl);
    hostlist_uniq (hl);
    while (hostlist_ranged_string (hl, len, s) < 0)
        if (!(s = realloc (s, len *= 2)))
            oom ();
    len = strlen (s);
    return s;
}

static void Jadd_hl (JSON o, const char *name, hostlist_t hl)
{
    char *s = hl_string (hl);
    Jadd_str (o, name, s);
    free (s);
}

static bool Jget_hl (JSON o, const char *name, hostlist_t *hp)
{
    hostlist_t hl;
    const char *s;

    if (!Jget_str (o, name, &s) || !(hl = hostlist_create (s)))
        return false;
    *hp = hl;
    return true;
}

static void ns_destroy (ns_t *ns)
{
    if (ns->ok)
        hostlist_destroy (ns->ok);
    if (ns->fail)
        hostlist_destroy (ns->fail);
    if (ns->slow)
        hostlist_destroy (ns->slow);
    if (ns->unknown)
        hostlist_destroy (ns->unknown);
    free (ns);
}

static ns_t *ns_create (const char *ok, const char *fail,
                        const char *slow, const char *unknown)
{
    ns_t *ns = xzmalloc (sizeof (*ns));

    ns->ok = hostlist_create (ok);
    ns->fail = hostlist_create (fail);
    ns->slow = hostlist_create (slow);
    ns->unknown = hostlist_create (unknown);
    if (!ns->ok || !ns->fail|| !ns->slow || !ns->unknown)
        oom ();
    return ns;
}


static JSON ns_tojson (ns_t *ns)
{
    JSON o = Jnew ();
    Jadd_hl (o, "ok", ns->ok);
    Jadd_hl (o, "fail", ns->fail);
    Jadd_hl (o, "slow", ns->slow);
    Jadd_hl (o, "unknown", ns->unknown);
    return o;
}

static ns_t *ns_fromjson (JSON o)
{
    ns_t *ns = xzmalloc (sizeof (*ns));

    if (!Jget_hl (o, "ok", &ns->ok) || !Jget_hl (o, "unknown", &ns->unknown)
     || !Jget_hl (o, "slow", &ns->slow) || !Jget_hl (o, "fail", &ns->fail)) {
        ns_destroy (ns);
        return NULL;
    }
    return ns;
}

static int ns_tokvs (ctx_t *ctx)
{
    JSON o = ns_tojson (ctx->ns);
    int rc = -1;

    if (kvs_put (ctx->h, "conf.live.status", o) < 0)
        goto done;
    if (kvs_commit (ctx->h) < 0)
        goto done;
    rc = 0;
done:
    Jput (o);
    return rc;
}

static int ns_fromkvs (ctx_t *ctx)
{
    JSON o = NULL;
    int rc = -1;

    if (kvs_get (ctx->h, "conf.live.status", &o) < 0)
        goto done;
    ctx->ns = ns_fromjson (o);
    rc = 0;
done:
    Jput (o);
    return rc;
}

/* If ctx->ns is uninitialized, initialize it, using kvs data if any.
 * If ctx->ns is initialized, write it to kvs.
 */
static int ns_sync (ctx_t *ctx)
{
    int rc = -1;
    bool writekvs = false;

    if (ctx->ns) {
        writekvs = true;
    } else if (ns_fromkvs (ctx) < 0) {
        char *ok = ctx->rankstr, *fail = "", *slow = "", *unknown;
        if (asprintf (&unknown, "1-%d", flux_size (ctx->h) - 1) < 0)
            oom ();
        ctx->ns = ns_create (ok, fail, slow, unknown);
        free (unknown);
        writekvs = true;
    }
    if (writekvs) {
        if (ns_tokvs (ctx) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

/* N.B. from=CS_UNKNOWN is treated as "from any other state".
 */
static void ns_chg_one (ctx_t *ctx, const char *s, cstate_t from, cstate_t to)
{
    if (from == CS_UNKNOWN)
        (void)hostlist_delete_host (ctx->ns->unknown, s);
    if (from == CS_UNKNOWN || from == CS_FAIL)
        (void)hostlist_delete_host (ctx->ns->fail, s);
    if (from == CS_UNKNOWN || from == CS_SLOW)
        (void)hostlist_delete_host (ctx->ns->slow, s);
    if (from == CS_UNKNOWN || from == CS_OK)
        (void)hostlist_delete_host (ctx->ns->ok, s);

    switch (to) {
        case CS_OK:
            if (!hostlist_push_host (ctx->ns->ok, s))
                oom ();
            break;
        case CS_SLOW:
            if (!hostlist_push_host (ctx->ns->slow, s))
                oom ();
            break;
        case CS_FAIL:
            if (!hostlist_push_host (ctx->ns->fail, s))
                oom ();
            break;
        case CS_UNKNOWN:
            if (!hostlist_push_host (ctx->ns->fail, s))
                oom ();
            break;
    }
}

/* Iterate through all children in JSON topology object resulting from
 * hello reduction, and transition them all to CS_OK.
 * FIXME: should we generate a live.cstate event if state is
 * transitioning from CS_SLOW or CS_FAIL e.g. after reparenting?
 */
static void ns_chg_hello (ctx_t *ctx, JSON a)
{
    json_object_iter iter;
    int i, len, crank;
    char *s;

    json_object_object_foreachC (a, iter) {
        if (Jget_ar_len (iter.val, &len))
            for (i = 0; i < len; i++) {
                if (Jget_ar_int (iter.val, i, &crank)) {
                    if (asprintf (&s, "%d", crank) < 0)
                        oom ();
                    ns_chg_one (ctx, s, CS_UNKNOWN, CS_OK);
                    free (s);
                }
            }
    }
}

/* Read ctx->topo from KVS.
 * Topology in the kvs is a JSON array of arrays.
 * Topology in ctx->topo is a JSON hash of arrays, for ease of merging.
 */
static int topo_fromkvs (ctx_t *ctx)
{
    JSON car, ar = NULL, topo = NULL;
    int rc = -1;
    int len, i;
    char *prank;

    if (kvs_get (ctx->h, "conf.live.topology", &ar) < 0)
        goto done;
    if (!Jget_ar_len (ar, &len))
        goto done;
    topo = Jnew ();
    for (i = 0; i < len; i++) {
        if (Jget_ar_obj (ar, i, &car)) {
            if (asprintf (&prank, "%d", i) < 0)
                oom ();
            Jadd_obj (topo, prank, car);
            free (prank);
        }
    }
    ctx->topo = Jget (topo);
    rc = 0;
done:
    Jput (ar);
    Jput (topo);
    return rc;
}

static int topo_tokvs (ctx_t *ctx)
{
    json_object_iter iter;
    JSON ar = Jnew_ar ();
    int rc = -1;

    json_object_object_foreachC (ctx->topo, iter) {
        int prank = strtoul (iter.key, NULL, 10);
        Jput_ar_obj (ar, prank, iter.val);
    }
    if (kvs_put (ctx->h, "conf.live.topology", ar) < 0)
        goto done;
    if (kvs_commit (ctx->h) < 0)
        goto done;
    rc = 0;
done:
    Jput (ar);
    return rc;
}

/* If ctx->topo is uninitialized, initialize it, using kvs data if any.
 * If ctx->topo is initialized, write it to kvs.
 */
static int topo_sync (ctx_t *ctx)
{
    int rc = -1;
    bool writekvs = false;

    if (ctx->topo) {
        writekvs = true;
    } else if (topo_fromkvs (ctx) < 0) {
        ctx->topo = Jnew ();
        writekvs = true;
    }
    if (writekvs) {
        if (topo_tokvs (ctx) < 0)
            goto done;
    }
    rc = 0;
done:
    return rc;
}

static bool inarray (JSON ar, int n)
{
    int i, len, val;

    if (Jget_ar_len (ar, &len))
        for (i = 0; i < len; i++)
            if (Jget_ar_int (ar, i, &val) && val == n)
                return true;
    return false;
}

/* Reduce b into a, where a and b look like:
 *    { "p1":[c1,c2,...], "p2":[c1,c2,...], ... }
 */
static void hello_merge (JSON a, JSON b)
{
    JSON ar;
    json_object_iter iter;
    int i, len, crank;

    /* Iterate thru parents in 'b'.  If parent exists in 'a', merge children,
     * else insert the whole parent from 'b' into 'a', with its children.
     */
    json_object_object_foreachC (b, iter) {
        if (Jget_obj (a, iter.key, &ar) && Jget_ar_len (iter.val, &len)) {
            for (i = 0; i < len; i++) {
                if (Jget_ar_int (iter.val, i, &crank)) {
                    if (!inarray (ar, crank))
                        Jadd_ar_int (ar, crank);
                }
            }
        } else
            Jadd_obj (a, iter.key, iter.val);
    }
}

static void hello_sink (flux_t h, void *item, int batchnum, void *arg)
{
    ctx_t *ctx = arg;
    JSON a = item;

    if (ctx->master) {
        ns_chg_hello (ctx, a);
        hello_merge (ctx->topo, a);
        if (ns_sync (ctx) < 0)
            flux_log (h, LOG_ERR, "%s: ns_sync: %s",
                      __FUNCTION__, strerror (errno));
        if (topo_sync (ctx) < 0)
            flux_log (h, LOG_ERR, "%s: topo_sync: %s",
                      __FUNCTION__, strerror (errno));
    } else {
        if (flux_request_send (h, a, "live.push") < 0)
            flux_log (h, LOG_ERR, "%s: flux_request_send: %s",
                      __FUNCTION__, strerror (errno));
    }
    Jput (a);
}

static void hello_reduce (flux_t h, zlist_t *items, int batchnum, void *arg)
{
    JSON a, b;

    if ((a = zlist_pop (items))) {
        while ((b = zlist_pop (items))) {
            hello_merge (a, b);
            Jput (b);
        }
        if (zlist_append (items, a) < 0)
            oom ();
    }
}

/* Source:  { "prank":[crank] }
 */
static void hello_source (ctx_t *ctx, const char *prank, int crank)
{
    JSON a = Jnew ();
    JSON c = Jnew_ar ();

    Jadd_ar_int (c, crank);
    Jadd_obj (a, prank, c);
    Jput (c);

    flux_red_append (ctx->r, a, 0);
}

static int push_request_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    ctx_t *ctx = arg;
    JSON request = NULL;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    flux_red_append (ctx->r, Jget (request), 0);
done:
    Jput (request);
    zmsg_destroy (zmsg);
    return 0;
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
    child_t *c;

    if (cmb_msg_decode (*zmsg, NULL, &request) < 0 || request == NULL
                            || !Jget_int (request, "rank", &rank)) {
        flux_log (ctx->h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    /* Create a record for this child, unless already seen.
     * Also send rank upstream (reduced) to update conf.live.state.
     */
    c = child_create (rank);
    if (zhash_insert (ctx->children, c->rankstr, c) == 0) {
        zhash_freefn (ctx->children, c->rankstr,(zhash_free_fn *)child_destroy);
        manage_subscriptions (ctx);
        hello_source (ctx, ctx->rankstr, rank);
    } else
        child_destroy (c);
    /* Construct response - built from our own hello response, if any.
     * We add ourselves temporarily, sans URI (see parents_fromjson()).
     */
    if (zlist_push (ctx->parents, parent_create (ctx->rank, NULL)) < 0)
        oom ();
    response = parents_tojson (ctx);
    parent_destroy (zlist_pop (ctx->parents));

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
    int rc;

    Jadd_int (request, "rank", ctx->rank);
    if (!(response = flux_rpc (ctx->h, request, "live.hello"))) {
        flux_log (ctx->h, LOG_ERR, "flux_rpc: %s", strerror (errno));
        goto done;
    }
    if (zlist_size (ctx->parents) == 0) /* don't redo on failover */
        parents_fromjson (ctx, response);
    rc = 0;
done:
    Jput (request);
    Jput (response);
    return 0;
}

static int failover_request_cb (flux_t h, int typemask, zmsg_t **zmsg,void *arg)
{
    ctx_t *ctx = arg;

    if (failover (ctx) < 0)
        flux_respond_errnum (h, zmsg, errno);
    else
        flux_respond_errnum (h, zmsg, 0);

    return 0;
}

static int recover_request_cb (flux_t h, int typemask, zmsg_t **zmsg,void *arg)
{
    ctx_t *ctx = arg;

    if (recover (ctx) < 0)
        flux_respond_errnum (h, zmsg, errno);
    else
        flux_respond_errnum (h, zmsg, 0);

    return 0;
}

static int recover_event_cb (flux_t h, int typemask, zmsg_t **zmsg,void *arg)
{
    ctx_t *ctx = arg;

    if (zlist_size (ctx->parents) > 0 && recover (ctx) < 0) {
        if (errno == EINVAL)
            flux_log (h, LOG_ERR, "recovery: parent is still in FAIL state");
        else
            flux_log (h, LOG_ERR, "recover: %s", strerror (errno));
    }
    zmsg_destroy (zmsg);
    return 0;
}

static msghandler_t htab[] = {
    { FLUX_MSGTYPE_EVENT,       "hb",                  hb_cb },
    { FLUX_MSGTYPE_EVENT,       "live.cstate",         cstate_cb },
    { FLUX_MSGTYPE_EVENT,       "live.recover",        recover_event_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.hello",          hello_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.goodbye",        goodbye_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.push",           push_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.failover",       failover_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.recover",        recover_request_cb},
};
const int htablen = sizeof (htab) / sizeof (htab[0]);

int mod_main (flux_t h, zhash_t *args)
{
    ctx_t *ctx = getctx (h);

    if (ctx->master) {
        if (ns_sync (ctx) < 0) {
            flux_log (h, LOG_ERR, "ns_sync: %s", strerror (errno));
            return -1;
        }
        if (topo_sync (ctx) < 0) {
            flux_log (h, LOG_ERR, "topo_sync: %s", strerror (errno));
            return -1;
        }
    } else {
        if (hello (ctx) < 0)
            return -1;
    }
    if (kvs_watch_int (h, "conf.live.max-idle", max_idle_cb, ctx) < 0) {
        flux_log (h, LOG_ERR, "kvs_watch_int %s: %s", "conf.live.max-idle",
                  strerror (errno));
        return -1;
    }
    if (kvs_watch_int (h, "conf.live.slow-idle", slow_idle_cb, ctx) < 0) {
        flux_log (h, LOG_ERR, "kvs_watch_int %s: %s", "conf.live.slow-idle",
                  strerror (errno));
        return -1;
    }
    if (flux_event_subscribe (h, "live.") < 0) {
        flux_log (ctx->h, LOG_ERR, "flux_event_subscribe: %s",
                  strerror (errno));
        return -1;
    }
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
