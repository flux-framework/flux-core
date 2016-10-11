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

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <czmq.h>
#include <jansson.h>

#include "attr.h"
#include "rpc.h"

#include "src/common/libutil/oom.h"
#include "src/common/libutil/xzmalloc.h"

typedef struct {
    zhash_t *hash;
    zlist_t *names;
    flux_t *h;
} ctx_t;

typedef struct {
    char *val;
    int flags;
} attr_t;

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    zhash_destroy (&ctx->hash);
    zlist_destroy (&ctx->names);
    free (ctx);
}

static ctx_t *getctx (flux_t *h)
{
    ctx_t *ctx = flux_aux_get (h, "flux::attr");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->hash = zhash_new ()))
            oom ();
        ctx->h = h;
        flux_aux_set (h, "flux::attr", ctx, freectx);
    }
    return ctx;
}

static void attr_destroy (void *arg)
{
    attr_t *attr = arg;
    free (attr->val);
    free (attr);
}

static attr_t *attr_create (const char *val, int flags)
{
    attr_t *attr = xzmalloc (sizeof (*attr));
    attr->val = xstrdup (val);
    attr->flags = flags;
    return attr;
}

static int attr_get_rpc (ctx_t *ctx, const char *name, attr_t **attrp)
{
    flux_rpc_t *r;
    const char *val;
    int flags;
    attr_t *attr;
    int rc = -1;

    if (!(r = flux_rpcf (ctx->h, "cmb.attrget", FLUX_NODEID_ANY, 0,
                         "{s:s}", "name", name)))
        goto done;
    if (flux_rpc_getf (r, "{s:s, s:i}", "value", &val, "flags", &flags) < 0)
        goto done;
    attr = attr_create (val, flags);
    zhash_update (ctx->hash, name, attr);
    zhash_freefn (ctx->hash, name, attr_destroy);
    *attrp = attr;
    rc = 0;
done:
    flux_rpc_destroy (r);
    return rc;
}

static int attr_set_rpc (ctx_t *ctx, const char *name, const char *val)
{
    flux_rpc_t *r;
    attr_t *attr;
    int rc = -1;

#if JANSSON_VERSION_HEX >= 0x020800
    /* $? format specifier was introduced in jansson 2.8 */
    r = flux_rpcf (ctx->h, "cmb.attrset", FLUX_NODEID_ANY, 0,
                   "{s:s, s:s?}", "name", name, "value", val);
#else
    r = flux_rpcf (ctx->h, "cmb.attrset", FLUX_NODEID_ANY, 0,
                   val ? "{s:s, s:s}" : "{s:s, s:n}",
                   "name", name, "value", val);
#endif
    if (!r)
        goto done;
    if (flux_rpc_get (r, NULL) < 0)
        goto done;
    if (val) {
        attr = attr_create (val, 0);
        zhash_update (ctx->hash, name, attr);
        zhash_freefn (ctx->hash, name, attr_destroy);
    } else
        zhash_delete (ctx->hash, name);
    rc = 0;
done:
    flux_rpc_destroy (r);
    return rc;
}

#if CZMQ_VERSION < CZMQ_MAKE_VERSION(3,0,1)
static bool attr_strcmp (const char *s1, const char *s2)
{
    return (strcmp (s1, s2) > 0);
}
#else
static int attr_strcmp (const char *s1, const char *s2)
{
    return strcmp (s1, s2);
}
#endif

static int attr_list_rpc (ctx_t *ctx)
{
    flux_rpc_t *r;
    json_t *array, *value;
    size_t index;
    int rc = -1;

    if (!(r = flux_rpc (ctx->h, "cmb.attrlist", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_getf (r, "{s:o}", "names", &array) < 0)
        goto done;
    zlist_destroy (&ctx->names);
    if (!(ctx->names = zlist_new ()))
        oom ();
#if JANSSON_VERSION_HEX >= 0x020500
    json_array_foreach (array, index, value) {
#else
    for (index = 0; index < json_array_size (array); index++) {
        value = json_array_get (array, index);
#endif
        const char *name = json_string_value (value);
        if (!name) {
            errno = EPROTO;
            goto done;
        }
        if (zlist_append (ctx->names, xstrdup (name)) < 0)
            oom ();
    }
    zlist_sort (ctx->names, (zlist_compare_fn *)attr_strcmp);
    rc = 0;
done:
    flux_rpc_destroy (r);
    return rc;
}

const char *flux_attr_get (flux_t *h, const char *name, int *flags)
{
    ctx_t *ctx = getctx (h);
    attr_t *attr;

    if (!(attr = zhash_lookup (ctx->hash, name))
                        || !(attr->flags & FLUX_ATTRFLAG_IMMUTABLE))
        if (attr_get_rpc (ctx, name, &attr) < 0)
            return NULL;
    if (flags && attr)
        *flags = attr->flags;
    return attr ? attr->val : NULL;
}

int flux_attr_set (flux_t *h, const char *name, const char *val)
{
    ctx_t *ctx = getctx (h);

    if (attr_set_rpc (ctx, name, val) < 0)
        return -1;
    return 0;
}

int flux_attr_fake (flux_t *h, const char *name, const char *val, int flags)
{
    ctx_t *ctx = getctx (h);
    attr_t *attr = attr_create (val, flags);
    zhash_update (ctx->hash, name, attr);
    zhash_freefn (ctx->hash, name, attr_destroy);
    return 0;
}

const char *flux_attr_first (flux_t *h)
{
    ctx_t *ctx = getctx (h);

    if (attr_list_rpc (ctx) < 0)
        return NULL;
    return ctx->names ? zlist_first (ctx->names) : NULL;
}

const char *flux_attr_next (flux_t *h)
{
    ctx_t *ctx = flux_aux_get (h, "flux::attr");

    return ctx->names ? zlist_next (ctx->names) : NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
