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

struct route_ctx_struct {
    zhash_t *route;
};

route_ctx_t route_init (void)
{
    route_ctx_t ctx = xzmalloc (sizeof (struct route_ctx_struct));

    if (!(ctx->route = zhash_new ()))
        oom ();

    return ctx;
}

void route_fini (route_ctx_t ctx)
{
    zhash_destroy (&ctx->route);
    free (ctx);
}

static route_t *_route_create (const char *gw, int flags)
{
    route_t *rte = xzmalloc (sizeof (route_t));
    rte->gw = xstrdup (gw);
    rte->flags = flags;
    return rte;
}

static void _free_route (route_t *rte)
{
    free (rte->gw);
    free (rte);
}

int route_add (route_ctx_t ctx, const char *dst, const char *gw, int flags)
{
    route_t *rte = _route_create (gw, flags);

    if (zhash_insert (ctx->route, dst, rte) < 0) {
        _free_route (rte);
        return -1;
    }
    zhash_freefn (ctx->route, dst, (zhash_free_fn *)_free_route);
    return 0;
}

void route_del (route_ctx_t ctx, const char *dst, const char *gw)
{
    route_t *rte = zhash_lookup (ctx->route, dst);

    if (rte && !strcmp (rte->gw, gw))
        zhash_delete (ctx->route, dst);
}

route_t *route_lookup (route_ctx_t ctx, const char *dst)
{
    return zhash_lookup (ctx->route, dst);
}

static int _route_to_json (const char *dst, route_t *rte, json_object *o)
{
    json_object *oo, *no;

    if (!(oo = json_object_new_object ()))
        oom ();
    if (!(no = json_object_new_string (dst)))
        oom ();
    json_object_object_add (oo, "dst", no);
    if (!(no = json_object_new_string (rte->gw)))
        oom ();
    json_object_object_add (oo, "gw", no);
    if (!(no = json_object_new_int (rte->flags)))
        oom ();
    json_object_object_add (oo, "flags", no);
    json_object_array_add (o, oo);
    return 0;
}

json_object *route_dump_json (route_ctx_t ctx)
{
    json_object *o;

    if (!(o = json_object_new_array ()))
        oom ();
    zhash_foreach (ctx->route, (zhash_foreach_fn *)_route_to_json, o);
    return o;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
