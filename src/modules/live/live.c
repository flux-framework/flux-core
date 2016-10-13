/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

/* livesrv.c - node liveness service */

/* This module builds on the following services in the cmbd:
 *   cmb.peers - get idle time (in heartbeats) for non-module peers
 *   cmb.failover - switch to new parent
 * The cmbd expects failover to be driven externally (e.g. by us).
 * The cmbd maintains a hash of peers and their idle time, and also sends
 * a keepalive upstream on the heartbeat if nothing else has been sent in the
 * previous epoch.  So if the idle time for a child is > 1, something is
 * probably wrong.
 *
 * In this module, parents monitor their children on the heartbeat.  That is,
 * we call cmb.peers (locally) and check the idle time of our children.
 * If a child changes state, we publish a live.cstate event, intended to
 * reach grandchildren so they can failover to new parent without relying on
 * upstream services which would be unavailable to them for the moment.
 * (N.B. Reaching grandchildren is problematic if events are being
 * distributed only via the TBON)
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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>
#include <ctype.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/modules/kvs/kvs_deprecated.h"

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/nodeset.h"


typedef enum { CS_OK, CS_SLOW, CS_FAIL, CS_UNKNOWN } cstate_t;

typedef struct {
    nodeset_t *ok;
    nodeset_t *fail;
    nodeset_t *slow;
    nodeset_t *unknown;
} ns_t;

typedef struct {
    uint32_t rank;
    char *uri;
    cstate_t state;
} parent_t;

typedef struct {
    uint32_t rank;
    char rankstr[16];
    cstate_t state;
} child_t;

typedef struct {
    int max_idle;
    int slow_idle;
    int epoch;
    uint32_t rank;
    char rankstr[16];
    zlist_t *parents;   /* current parent is first in list */
    zhash_t *children;
    bool hb_subscribed;
    flux_reduce_t *r;
    ns_t *ns;           /* master only */
    json_object *topo;          /* master only */
    flux_t *h;
} ctx_t;

static void parent_destroy (parent_t *p);
static void child_destroy (child_t *c);
static int hello (ctx_t *ctx);
static void goodbye (ctx_t *ctx, int parent_rank);
static void manage_subscriptions (ctx_t *ctx);

static const int default_max_idle = 5;
static const int default_slow_idle = 3;
static const double reduce_timeout = 0.800;

static void ns_chg_one (ctx_t *ctx, uint32_t r, cstate_t from, cstate_t to);
static int ns_sync (ctx_t *ctx);

static void hello_destroy (void *arg);
static void hello_forward (flux_reduce_t *r, int batchnum, void *arg);
static void hello_sink (flux_reduce_t *r, int batchnum, void *arg);
static void hello_reduce (flux_reduce_t *r, int batchnum, void *arg);

static struct flux_reduce_ops hello_ops = {
    .destroy = hello_destroy,
    .reduce = hello_reduce,
    .sink = hello_sink,
    .forward = hello_forward,
    .itemweight = NULL,
};

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    parent_t *p;

    if (ctx) {
        if (ctx->parents) {
            while ((p = zlist_pop (ctx->parents)))
                parent_destroy (p);
            zlist_destroy (&ctx->parents);
        }
        zhash_destroy (&ctx->children);
        flux_reduce_destroy (ctx->r);
        if (ctx->topo)
            Jput (ctx->topo);
        free (ctx);
    }
}

static ctx_t *getctx (flux_t *h)
{
    ctx_t *ctx = flux_aux_get (h, "flux::live");
    int n;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        ctx->max_idle = default_max_idle;
        ctx->slow_idle = default_slow_idle;
        if (flux_get_rank (h, &ctx->rank) < 0) {
            flux_log_error (h, "flux_get_rank");
            goto error;
        }
        n = snprintf (ctx->rankstr, sizeof (ctx->rankstr), "%d", ctx->rank);
        assert (n < sizeof (ctx->rankstr));
        if (!(ctx->parents = zlist_new ()) || !(ctx->children = zhash_new ())) {
            flux_log_error (h, "zlist_new/zhash_new");
            goto error;
        }
        /* FIXME: reduction is no longer scaled by TBON height.
         * If we need this, it will need to be calculated here.
         */
        ctx->r = flux_reduce_create (h, hello_ops,
                                     reduce_timeout, ctx,
                                     FLUX_REDUCE_TIMEDFLUSH);
        if (!ctx->r) {
            flux_log_error (h, "flux_reduce_create");
            goto error;
        }
        ctx->h = h;
        flux_aux_set (h, "flux::live", ctx, freectx);
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static void child_destroy (child_t *c)
{
    free (c);
}

static child_t *child_create (int rank)
{
    child_t *c = xzmalloc (sizeof (*c));
    int n;

    c->rank = rank;
    n = snprintf (c->rankstr, sizeof (c->rankstr), "%d", rank);
    assert (n < sizeof (c->rankstr));
    return c;
}

static void parent_destroy (parent_t *p)
{
    if (p) {
        if (p->uri)
            free (p->uri);
        free (p);
    }
}

static void parent_set_uri (parent_t *p, const char *uri)
{
    if (p->uri)
        free (p->uri);
    p->uri = uri ? xstrdup (uri) : NULL;
}

static parent_t *parent_create (int rank, const char *uri)
{
    parent_t *p = xzmalloc (sizeof (*p));
    p->rank = rank;
    parent_set_uri (p, uri);
    return p;
}

static parent_t *parent_fromjson (json_object *o)
{
    int rank;
    const char *uri = NULL;
    if (!Jget_int (o, "rank", &rank))
        return NULL;
    (void)Jget_str (o, "uri", &uri); /* optional */
    return parent_create (rank, uri);
}

static json_object *parent_tojson (parent_t *p)
{
    json_object *o = Jnew ();

    Jadd_int (o, "rank", p->rank);
    if (p->uri)
        Jadd_str (o, "uri", p->uri);
    return o;
}

static json_object *parents_tojson (ctx_t *ctx)
{
    parent_t *p;
    json_object *el;
    json_object *ar = Jnew_ar ();

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
static void parents_fromjson (ctx_t *ctx, json_object *ar)
{
    int i, len;
    json_object *el;
    parent_t *p;

    if (Jget_ar_len (ar, &len)) {
        for (i = 0; i < len; i++) {
            if (Jget_ar_obj (ar, i, &el) && (p = parent_fromjson (el))) {
                if (i == 0) {
                    const char *uri = flux_attr_get (ctx->h,
                                                     "tbon-parent-uri", NULL);
                    parent_set_uri (p, uri);
                }
                flux_log (ctx->h, LOG_DEBUG, "parent[%d] %d %s",
                          i, p->rank, p->uri ? p->uri : "NULL");
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
        flux_log_error (ctx->h, "%s %s", __FUNCTION__, p->uri);
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

static void cstate_cb (flux_t *h, flux_msg_handler_t *w,
                       const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    json_object *event = NULL;
    int epoch, parent, rank;
    cstate_t ostate, nstate;

    if (flux_event_decode (msg, NULL, &json_str) < 0
            || !(event = Jfromstr (json_str))
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
    if (ctx->rank == 0) {
        ns_chg_one (ctx, rank, ostate, nstate);
        if (ns_sync (ctx) < 0)
            flux_log_error (h, "%s: ns_sync", __FUNCTION__);
    }
done:
    Jput (event);
}

static void cstate_change (ctx_t *ctx, child_t *c, cstate_t newstate)
{
    json_object *event = Jnew ();
    flux_msg_t *msg;

    flux_log (ctx->h, LOG_CRIT, "transitioning %d from %s to %s", c->rank,
                c->state == CS_OK ? "OK" :
                c->state == CS_SLOW ? "SLOW" :
                c->state == CS_FAIL ? "FAIL" : "UNKNOWN",
                newstate == CS_OK ? "OK" :
                newstate == CS_SLOW ? "SLOW" :
                newstate == CS_FAIL ? "FAIL" : "UNKNOWN");

    Jadd_int (event, "rank", c->rank);
    Jadd_int (event, "ostate", c->state);
    c->state = newstate;
    Jadd_int (event, "nstate", c->state);
    Jadd_int (event, "parent", ctx->rank);
    Jadd_int (event, "epoch", ctx->epoch);
    if (!(msg = flux_event_encode ("live.cstate", Jtostr (event)))
              || flux_send (ctx->h, msg, 0) < 0) {
        flux_log_error (ctx->h, "%s: error sending event", __FUNCTION__);
    }
    flux_msg_destroy (msg);
    Jput (event);
}

/* On each heartbeat, check idle for downstream peers.
 * Note: lspeer returns a json_str that we convert a json_object,
 * which is indexed by peer socket id.
 * The socket id is the stringified rank for cmbds.
 */
static void hb_cb (flux_t *h, flux_msg_handler_t *w,
                   const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    char *peers_str = NULL;
    json_object *peers = NULL;
    zlist_t *keys = NULL;
    char *key;

    if (flux_heartbeat_decode (msg, &ctx->epoch) < 0) {
        flux_log (h, LOG_ERR, "%s: bad message", __FUNCTION__);
        goto done;
    }
    if (!(peers_str = flux_lspeer (h, -1)) || !(peers = Jfromstr (peers_str))) {
        flux_log_error (h, "flux_lspeer");
        goto done;
    }
    if (!(keys = zhash_keys (ctx->children)))
        oom ();
    key = zlist_first (keys);
    while (key) {
        json_object *co;
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
    Jput (peers);
    if (peers_str)
        free (peers_str);
}

static void manage_subscriptions (ctx_t *ctx)
{
    if (ctx->hb_subscribed && zhash_size (ctx->children) == 0) {
        if (flux_event_unsubscribe (ctx->h, "hb") < 0)
            flux_log_error (ctx->h, "%s: flux_event_unsubscribe hb",
                            __FUNCTION__);
        else
            ctx->hb_subscribed = false;
    } else if (!ctx->hb_subscribed && zhash_size (ctx->children) > 0) {
        if (flux_event_subscribe (ctx->h, "hb") < 0)
            flux_log_error (ctx->h, "%s: flux_event_subscribe hb",
                            __FUNCTION__);
        else
            ctx->hb_subscribed = true;
    }
}

static int max_idle_cb (const char *key, int val, void *arg, int errnum)
{
    ctx_t *ctx = arg;

    if (errnum != ENOENT && errnum != 0)
        return 0;
    if (errnum == ENOENT)
        val = default_max_idle;
    ctx->max_idle = val;
    return 0;
}

static int slow_idle_cb (const char *key, int val, void *arg, int errnum)
{
    ctx_t *ctx = arg;

    if (errnum != ENOENT && errnum != 0)
        return 0;
    if (errnum == ENOENT)
        val = default_slow_idle;
    ctx->slow_idle = val;
    return 0;
}

/* Goodbye request is fire and forget.
 */
static void goodbye_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;
    int n, rank, prank;
    char rankstr[16];

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "parent-rank", &prank)
                                    || !Jget_int (in, "rank", &rank)) {
        errno = EPROTO;
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    if (prank != ctx->rank) { /* in case misdirected to new parent */
        errno = EPROTO;
        flux_log_error (h, "%s: misdirected request", __FUNCTION__);
        goto done;
    }
    n = snprintf (rankstr, sizeof (rankstr), "%d", rank);
    assert (n < sizeof (rankstr));
    zhash_delete (ctx->children, rankstr);
    manage_subscriptions (ctx);
done:
    Jput (in);
}

static void goodbye (ctx_t *ctx, int parent_rank)
{
    json_object *in = Jnew ();
    flux_rpc_t *rpc;

    Jadd_int (in, "rank", ctx->rank);
    Jadd_int (in, "parent-rank", parent_rank);
    if (!(rpc = flux_rpc (ctx->h, "live.goodbye", Jtostr (in),
                          FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
        flux_log_error (ctx->h, "%s: flux_rpc", __FUNCTION__);
    flux_rpc_destroy (rpc);
    Jput (in);
}

static void ns_destroy (ns_t *ns)
{
    if (ns->ok)
        nodeset_destroy (ns->ok);
    if (ns->fail)
        nodeset_destroy (ns->fail);
    if (ns->slow)
        nodeset_destroy (ns->slow);
    if (ns->unknown)
        nodeset_destroy (ns->unknown);
    free (ns);
}

static ns_t *ns_create (const char *ok, const char *fail,
                        const char *slow, const char *unknown)
{
    ns_t *ns = xzmalloc (sizeof (*ns));

    ns->ok = nodeset_create_string (ok);
    ns->fail = nodeset_create_string (fail);
    ns->slow = nodeset_create_string (slow);
    ns->unknown = nodeset_create_string (unknown);
    if (!ns->ok || !ns->fail|| !ns->slow || !ns->unknown)
        oom ();
    return ns;
}


static json_object *ns_tojson (ns_t *ns)
{
    json_object *o = Jnew ();
    Jadd_str (o, "ok", nodeset_string (ns->ok));
    Jadd_str (o, "fail", nodeset_string (ns->fail));
    Jadd_str (o, "slow", nodeset_string (ns->slow));
    Jadd_str (o, "unknown", nodeset_string (ns->unknown));
    return o;
}

static ns_t *ns_fromjson (json_object *o)
{
    ns_t *ns = xzmalloc (sizeof (*ns));
    const char *s;

    if (!Jget_str (o, "ok", &s)      || !(ns->ok      = nodeset_create_string (s))
     || !Jget_str (o, "unknown", &s) || !(ns->unknown = nodeset_create_string (s))
     || !Jget_str (o, "slow", &s)    || !(ns->slow    = nodeset_create_string (s))
     || !Jget_str (o, "fail", &s)    || !(ns->fail    = nodeset_create_string (s))) {
        ns_destroy (ns);
        return NULL;
    }
    return ns;
}

static int ns_tokvs (ctx_t *ctx)
{
    json_object *o = ns_tojson (ctx->ns);
    int rc = -1;

    if (kvs_put (ctx->h, "conf.live.status", Jtostr (o)) < 0)
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
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (ctx->h, "conf.live.status", &json_str) < 0
                                        || !(o = Jfromstr (json_str)))
        goto done;
    ctx->ns = ns_fromjson (o);
    rc = 0;
done:
    if (json_str)
        free (json_str);
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
        char *ok = ctx->rankstr, *fail = "", *slow = "", *unknown = "";
        uint32_t size;
        if (flux_get_size (ctx->h, &size) < 0)
            goto done;
        if (size > 1)
            if (asprintf (&unknown, "1-%d", size - 1) < 0)
                oom ();
        ctx->ns = ns_create (ok, fail, slow, unknown);
        if (size > 1)
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
static void ns_chg_one (ctx_t *ctx, uint32_t r, cstate_t from, cstate_t to)
{
    if (from == CS_UNKNOWN)
        nodeset_delete_rank (ctx->ns->unknown, r);
    if (from == CS_UNKNOWN || from == CS_FAIL)
        nodeset_delete_rank (ctx->ns->fail, r);
    if (from == CS_UNKNOWN || from == CS_SLOW)
        nodeset_delete_rank (ctx->ns->slow, r);
    if (from == CS_UNKNOWN || from == CS_OK)
        nodeset_delete_rank (ctx->ns->ok, r);

    switch (to) {
        case CS_OK:
            nodeset_add_rank (ctx->ns->ok, r);
            break;
        case CS_SLOW:
            nodeset_add_rank (ctx->ns->slow, r);
            break;
        case CS_FAIL:
            nodeset_add_rank (ctx->ns->fail, r);
            break;
        case CS_UNKNOWN:
            nodeset_add_rank (ctx->ns->fail, r);
            break;
    }
}

/* Iterate through all children in JSON topology object resulting from
 * hello reduction, and transition them all to CS_OK.
 * FIXME: should we generate a live.cstate event if state is
 * transitioning from CS_SLOW or CS_FAIL e.g. after reparenting?
 */
static void ns_chg_hello (ctx_t *ctx, json_object *a)
{
    json_object_iter iter;
    int i, len, crank;

    json_object_object_foreachC (a, iter) {
        if (Jget_ar_len (iter.val, &len))
            for (i = 0; i < len; i++) {
                if (Jget_ar_int (iter.val, i, &crank))
                    ns_chg_one (ctx, crank, CS_UNKNOWN, CS_OK);
            }
    }
}

/* Read ctx->topo from KVS.
 * Topology in the kvs is a JSON array of arrays.
 * Topology in ctx->topo is a JSON hash of arrays, for ease of merging.
 */
static int topo_fromkvs (ctx_t *ctx)
{
    char *json_str = NULL;
    json_object *car;
    json_object *ar = NULL;
    json_object *topo = NULL;
    int rc = -1;
    int n, len, i;
    char prank[16];

    if (kvs_get (ctx->h, "conf.live.topology", &json_str) < 0)
        goto done;
    if (!(ar = Jfromstr (json_str)) || !Jget_ar_len (ar, &len))
        goto done;
    topo = Jnew ();
    for (i = 0; i < len; i++) {
        if (Jget_ar_obj (ar, i, &car)) {
            n = snprintf (prank, sizeof (prank), "%d", i);
            assert (n < sizeof (prank));
            Jadd_obj (topo, prank, car);
        }
    }
    ctx->topo = Jget (topo);
    rc = 0;
done:
    Jput (ar);
    Jput (topo);
    if (json_str)
        free (json_str);
    return rc;
}

static int topo_tokvs (ctx_t *ctx)
{
    json_object_iter iter;
    json_object *ar = Jnew_ar ();
    int rc = -1;

    json_object_object_foreachC (ctx->topo, iter) {
        int prank = strtoul (iter.key, NULL, 10);
        Jput_ar_obj (ar, prank, iter.val);
    }
    if (kvs_put (ctx->h, "conf.live.topology", Jtostr (ar)) < 0)
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

static bool inarray (json_object *ar, int n)
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
static void hello_merge (json_object *a, json_object *b)
{
    json_object *ar;
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

static void hello_destroy (void *arg)
{
    json_object *o = arg;
    Jput (o);
}

static void hello_forward (flux_reduce_t *r, int batchnum, void *arg)
{
    ctx_t *ctx = arg;
    flux_rpc_t *rpc;
    json_object *o;

    while ((o = flux_reduce_pop (r))) {
        if (!(rpc = flux_rpc (ctx->h, "live.push", Jtostr (o),
                              FLUX_NODEID_UPSTREAM, FLUX_RPC_NORESPONSE)))
            flux_log_error (ctx->h, "%s: flux_rpc", __FUNCTION__);
        else
            flux_rpc_destroy (rpc);
        Jput (o);
    }
}

static void hello_sink (flux_reduce_t *r, int batchnum, void *arg)
{
    ctx_t *ctx = arg;
    json_object *o;

    while ((o = flux_reduce_pop (r))) {
        ns_chg_hello (ctx, o);
        hello_merge (ctx->topo, o);
        if (ns_sync (ctx) < 0)
            flux_log_error (ctx->h, "%s: ns_sync", __FUNCTION__);
        if (topo_sync (ctx) < 0)
            flux_log_error (ctx->h, "%s: topo_sync", __FUNCTION__);
        Jput (o);
    }
}

static void hello_reduce (flux_reduce_t *r, int batchnum, void *arg)
{
    json_object *a;
    json_object *b;

    if ((a = flux_reduce_pop (r))) {
        while ((b = flux_reduce_pop (r))) {
            hello_merge (a, b);
            Jput (b);
        }
        if (flux_reduce_push (r, a) < 0)
            oom ();
    }
}

/* Source:  { "prank":[crank] }
 */
static void hello_source (ctx_t *ctx, const char *prank, int crank)
{
    json_object *a = Jnew ();
    json_object *c = Jnew_ar ();

    Jadd_ar_int (c, crank);
    Jadd_obj (a, prank, c);
    Jput (c);

    flux_reduce_append (ctx->r, a, 0);
}

/* push request is fire and forget.
 */
static void push_request_cb (flux_t *h, flux_msg_handler_t *w,
                             const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        flux_log_error (ctx->h, "%s: reuqest decode", __FUNCTION__);
        goto done;
    }
    if (!(in = Jfromstr (json_str))) {
        errno = EPROTO;
        flux_log_error (ctx->h, "%s: reuqest decode", __FUNCTION__);
        goto done;
    }
    flux_reduce_append (ctx->r, Jget (in), 0);
done:
    Jput (in);
}

/* hello: parents discover their children, and children discover their
 * grandparents which are potential failover candidates.
 */
static void hello_request_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    const char *json_str;
    json_object *in = NULL;
    json_object *out = NULL;
    int saved_errno;
    int rank, rc = -1;
    child_t *c;

    if (flux_request_decode (msg, NULL, &json_str) < 0) {
        saved_errno = EPROTO;
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    if (!(in = Jfromstr (json_str)) || !Jget_int (in, "rank", &rank)) {
        saved_errno = errno = EPROTO;
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    flux_log (h, LOG_DEBUG, "hello from %" PRIu32, rank);
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
    out = Jnew ();
    json_object_object_add (out, "parents", parents_tojson (ctx));
    parent_destroy (zlist_pop (ctx->parents));
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? saved_errno : 0,
                              rc < 0 ? NULL : Jtostr (out)) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    Jput (in);
    Jput (out);
}

/* Request: {"rank":N}
 * Response: {"parents":[...]}
 */
static int hello (ctx_t *ctx)
{
    const char *json_str;
    json_object *in = Jnew ();
    json_object *a;
    json_object *out = NULL;
    flux_rpc_t *rpc;
    int rc = -1;

    Jadd_int (in, "rank", ctx->rank);
    if (!(rpc = flux_rpc (ctx->h, "live.hello", Jtostr (in),
                          FLUX_NODEID_UPSTREAM, 0))) {
        flux_log_error (ctx->h, "%s: flux_rpc", __FUNCTION__);
        goto done;
    }
    if (flux_rpc_get (rpc, &json_str) < 0) {
        flux_log_error (ctx->h, "live.hello");
        goto done;
    }
    if (!(out = Jfromstr (json_str)) || !Jget_obj (out, "parents", &a)) {
        errno = EPROTO;
        flux_log_error (ctx->h, "live.hello");
        goto done;
    }
    if (zlist_size (ctx->parents) == 0) /* don't redo on failover */
        parents_fromjson (ctx, a);
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    Jput (in);
    Jput (out);
    return rc;
}

static void failover_request_cb (flux_t *h, flux_msg_handler_t *w,
                                 const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    if (failover (ctx) < 0)
        goto done; /* ESRCH (no failover candidate */
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void recover_request_cb (flux_t *h, flux_msg_handler_t *w,
                                const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;
    int rc = -1;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        flux_log_error (h, "%s: request decode", __FUNCTION__);
        goto done;
    }
    if (recover (ctx) < 0)
        goto done;
    rc = 0;
done:
    if (flux_respond (h, msg, rc < 0 ? errno : 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
}

static void recover_event_cb (flux_t *h, flux_msg_handler_t *w,
                              const flux_msg_t *msg, void *arg)
{
    ctx_t *ctx = arg;

    if (zlist_size (ctx->parents) > 0 && recover (ctx) < 0) {
        if (errno == EINVAL)
            flux_log (h, LOG_ERR, "recovery: parent is still in FAIL state");
        else
            flux_log_error (h, "recover");
    }
}

static struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_EVENT,       "hb",                  hb_cb },
    { FLUX_MSGTYPE_EVENT,       "live.cstate",         cstate_cb },
    { FLUX_MSGTYPE_EVENT,       "live.recover",        recover_event_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.hello",          hello_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.goodbye",        goodbye_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.push",           push_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.failover",       failover_request_cb },
    { FLUX_MSGTYPE_REQUEST,     "live.recover",        recover_request_cb},
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    int rc = -1;
    ctx_t *ctx = getctx (h);
    int i, barrier_count = 0;
    const char *barrier_name = "live-init";

    if (!ctx)
        goto done;
    for (i = 0; i < argc; i++) {
        if (!strncmp (argv[i], "barrier-count=", 14)) {
            barrier_count = strtoul (argv[i]+14, NULL, 10);
        } else if (!strncmp (argv[i], "barrier-name=", 13)) {
            barrier_name = argv[i]+13;
        } else
            flux_log (h, LOG_ERR, "ignoring unknown option: %s", argv[i]);
    }

    if (barrier_count > 0) {
        if (flux_barrier (h, barrier_name, barrier_count) < 0) {
            flux_log (h, LOG_ERR, "flux_barrier %s:%d",
                      barrier_name, barrier_count);
            goto done;
        }
        flux_log (h, LOG_DEBUG, "completed barrier %s:%d",
                  barrier_name, barrier_count);
    }

    if (ctx->rank == 0) {
        if (ns_sync (ctx) < 0) {
            flux_log_error (h, "ns_sync");
            goto done;
        }
        if (topo_sync (ctx) < 0) {
            flux_log_error (h, "topo_sync");
            goto done;
        }
    } else {
        if (hello (ctx) < 0)
            goto done;
    }
    if (kvs_watch_int (h, "conf.live.max-idle", max_idle_cb, ctx) < 0) {
        flux_log_error (h, "kvs_watch_int %s", "conf.live.max-idle");
        goto done;
    }
    if (kvs_watch_int (h, "conf.live.slow-idle", slow_idle_cb, ctx) < 0) {
        flux_log_error (h, "kvs_watch_int %s", "conf.live.slow-idle");
        goto done;
    }
    if (flux_event_subscribe (h, "live.cstate") < 0
                       || flux_event_subscribe (h, "live.recover") < 0) {
        flux_log_error (h, "flux_event_subscribe");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, ctx) < 0) {
        flux_log_error (h, "flux_msg_handler_advec");
        goto done;
    }
    if (flux_reactor_run (flux_get_reactor (h), 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done_delvec;
    }
    rc = 0;
done_delvec:
    flux_msg_handler_delvec (htab);
done:
    return rc;
}

MOD_NAME ("live");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
