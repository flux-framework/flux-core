/* route.c - routing table */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>
#include <sys/param.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <json/json.h>
#include <czmq.h>

#include "log.h"
#include "zmq.h"
#include "route.h"
#include "cmbd.h"
#include "cmb.h"
#include "util.h"

typedef struct {
    char *gw;
    char *parent;
    int flags;
} route_t;

struct route_ctx_struct {
    zhash_t *route;
    bool verbose; 
};

route_ctx_t route_init (bool verbose)
{
    route_ctx_t ctx = xzmalloc (sizeof (struct route_ctx_struct));

    if (!(ctx->route = zhash_new ()))
        oom ();
    ctx->verbose = verbose;

    return ctx;
}

void route_fini (route_ctx_t ctx)
{
    zhash_destroy (&ctx->route);
    free (ctx);
}

static route_t *_route_create (const char *gw, const char *parent, int flags)
{
    route_t *rte = NULL;

    if (gw) {
        rte = xzmalloc (sizeof (route_t));
        rte->gw = xstrdup (gw);
        if (parent)
            rte->parent = xstrdup (parent);
        rte->flags = flags;
    }
    return rte;
}

static void _free_route (route_t *rte)
{
    if (rte->parent)
        free (rte->parent);
    free (rte->gw);
    free (rte);
}

void route_add (route_ctx_t ctx, const char *dst, const char *gw,
                const char *parent, int flags)
{
    route_t *rte = _route_create (gw, parent, flags);

    if (rte) {
        zhash_update (ctx->route, dst, rte);
        zhash_freefn (ctx->route, dst, (zhash_free_fn *)_free_route);
        if (ctx->verbose)
            msg ("%s: %s via %s", __FUNCTION__, dst, gw);
    }
}

void route_del (route_ctx_t ctx, const char *dst, const char *gw)
{
    route_t *rte = zhash_lookup (ctx->route, dst);

    if (rte && (gw == NULL || !strcmp (rte->gw, gw))) {
        if (ctx->verbose)
            msg ("%s: %s via %s", __FUNCTION__, dst, rte->gw);
        zhash_delete (ctx->route, dst);
    }
}

const char *route_lookup (route_ctx_t ctx, const char *dst)
{
    route_t *rte = zhash_lookup (ctx->route, dst);
    return rte ? rte->gw : NULL;
}

static void _add_subtree_json (route_ctx_t ctx, json_object *o)
{
    json_object *vo, *oo, *dst, *gw, *parent, *flags;
    int i;

    if ((oo = json_object_object_get (o, "route"))) {
        for (i = 0; i < json_object_array_length (oo); i++) {
            vo = json_object_array_get_idx (oo, i);
            dst = json_object_object_get (vo, "dst");
            gw = json_object_object_get (vo, "gw");
            parent = json_object_object_get (vo, "parent");
            flags = json_object_object_get (vo, "flags");

            if (dst && gw) {
                route_add (ctx, json_object_get_string (dst),
                                json_object_get_string (gw),
                                parent ? json_object_get_string (parent) : NULL,
                                flags  ? json_object_get_int (flags) : 0);
            }
        }
    }
}

void route_add_hello (route_ctx_t ctx, zmsg_t *zmsg, int flags)
{
    zframe_t *zf;
    char *s, *first = NULL, *prev = NULL;
    json_object *o = NULL;

    zf = zmsg_first (zmsg);
    while (zf && zframe_size (zf) != 0) {
        if (!(s = zframe_strdup (zf)))
            oom ();
        if (!first)
            first = xstrdup (s);
        route_add (ctx, s, first, prev, flags);
        if (prev)
            free (prev);
        prev = s;
        zf = zmsg_next (zmsg);
    }
    if (first)
        free (first);
    if (prev)
        free (prev);

    if (cmb_msg_decode (zmsg, NULL, &o, NULL, NULL) == 0 && o != NULL) {
        _add_subtree_json (ctx, o);
        json_object_put (o);
    }
}

static void _subtree_append (route_ctx_t ctx, const char *rank, zlist_t *rmq);

typedef struct {
    route_ctx_t ctx;
    zlist_t *rmq;
    const char *parent;
} marg_t;

static int _match_subtree (const char *rank, route_t *rte, marg_t *arg)
{
    if (rte->parent && !strcmp (rte->parent, arg->parent)) {
        zlist_append (arg->rmq, xstrdup (rank));
        _subtree_append (arg->ctx, rank, arg->rmq);
    }
    return 0;
}
static void _subtree_append (route_ctx_t ctx, const char *rank, zlist_t *rmq)
{
    marg_t marg = { .ctx = ctx, .rmq = rmq, .parent = rank };

    zhash_foreach (ctx->route, (zhash_foreach_fn *)_match_subtree, &marg);
}

void route_del_subtree (route_ctx_t ctx, const char *rank)
{
    zlist_t *rmq;
    char *item;

    if (!(rmq = zlist_new ()))
        oom ();
    zlist_append (rmq, xstrdup (rank));
    _subtree_append (ctx, rank, rmq);

    while ((item = zlist_pop (rmq))) {
        route_del (ctx, item, NULL);
        free (item);
    }
    zlist_destroy (&rmq);
}

typedef struct {
    json_object *o;
    bool private;
} jarg_t;

static int _route_to_json (const char *dst, route_t *rte, jarg_t *arg)
{
    json_object *oo, *no;

    if (arg->private || !(rte->flags & ROUTE_FLAGS_PRIVATE)) {
        if (!(oo = json_object_new_object ()))
            oom ();
        if (!(no = json_object_new_string (dst)))
            oom ();
        json_object_object_add (oo, "dst", no);
        if (!(no = json_object_new_string (rte->gw)))
            oom ();
        json_object_object_add (oo, "gw", no);
        if (rte->parent) {
            if (!(no = json_object_new_string (rte->parent)))
                oom ();
            json_object_object_add (oo, "parent", no);
        }
        if (!(no = json_object_new_int (rte->flags)))
            oom ();
        json_object_object_add (oo, "flags", no);

        json_object_array_add (arg->o, oo);
    }
    return 0;
}

json_object *route_dump_json (route_ctx_t ctx, bool private)
{
    jarg_t arg = { .private = private };

    if (!(arg.o = json_object_new_array ()))
        oom ();
    zhash_foreach (ctx->route, (zhash_foreach_fn *)_route_to_json, &arg);

    return arg.o;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
