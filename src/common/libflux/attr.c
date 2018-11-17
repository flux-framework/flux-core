/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
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
#include "attr_private.h"
#include "rpc.h"

typedef struct {
    zhash_t *hash;
    flux_t *h;
} attr_ctx_t;

typedef struct {
    char *val;
    int flags;
} attr_t;

static void freectx (void *arg)
{
    attr_ctx_t *ctx = arg;
    if (ctx) {
        int saved_errno = errno;
        zhash_destroy (&ctx->hash);
        free (ctx);
        errno = saved_errno;
    }
}

static attr_ctx_t *attr_ctx_new (flux_t *h)
{
    attr_ctx_t *ctx = calloc (1, sizeof (*ctx));
    if (!ctx || !(ctx->hash = zhash_new ())) {
        /* Ensure errno set properly, in case zhash_new doesn't do it */
        errno = ENOMEM;
        goto error;
    }
    ctx->h = h;
    if (flux_aux_set (h, "flux::attr", ctx, freectx) < 0)
        goto error;
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static attr_ctx_t *getctx (flux_t *h)
{
    attr_ctx_t *ctx = flux_aux_get (h, "flux::attr");
    if (!ctx)
        ctx = attr_ctx_new (h);
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
    attr_t *attr = calloc (1, sizeof (*attr));
    if (!attr || !(attr->val = strdup (val))) {
        free (attr);
        errno = ENOMEM;
        return NULL;
    }
    attr->flags = flags;
    return attr;
}

static int attr_get_rpc (attr_ctx_t *ctx, const char *name, attr_t **attrp)
{
    flux_future_t *f;
    const char *val;
    int flags;
    attr_t *attr;
    int rc = -1;

    if (!(f = flux_rpc_pack (ctx->h, "attr.get", FLUX_NODEID_ANY, 0,
                             "{s:s}", "name", name)))
        goto done;
    if (flux_rpc_get_unpack (f, "{s:s, s:i}",
                             "value", &val, "flags", &flags) < 0)
        goto done;
    if (!(attr = attr_create (val, flags)))
        goto done;
    zhash_update (ctx->hash, name, attr);
    zhash_freefn (ctx->hash, name, attr_destroy);
    *attrp = attr;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int attr_set_rpc (attr_ctx_t *ctx, const char *name, const char *val)
{
    flux_future_t *f;
    attr_t *attr;
    int rc = -1;

    f = flux_rpc_pack (ctx->h, "attr.set", FLUX_NODEID_ANY, 0,
                       "{s:s, s:s}", "name", name, "value", val);
    if (!f)
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    if (!(attr = attr_create (val, 0)))
        goto done;
    zhash_update (ctx->hash, name, attr);
    zhash_freefn (ctx->hash, name, attr_destroy);
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int attr_rm_rpc (attr_ctx_t *ctx, const char *name)
{
    flux_future_t *f;
    int rc = -1;

    f = flux_rpc_pack (ctx->h, "attr.rm", FLUX_NODEID_ANY, 0,
                       "{s:s}", "name", name);
    if (!f)
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    zhash_delete (ctx->hash, name);
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

const char *flux_attr_get (flux_t *h, const char *name)
{
    attr_ctx_t *ctx;
    attr_t *attr;

    if (!h || !name) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = getctx (h)))
        return NULL;
    if (!(attr = zhash_lookup (ctx->hash, name))
                        || !(attr->flags & FLUX_ATTRFLAG_IMMUTABLE)) {
        if (attr_get_rpc (ctx, name, &attr) < 0)
            return NULL;
    }
    return attr->val;
}

int flux_attr_set (flux_t *h, const char *name, const char *val)
{
    int rc;
    if (!h || !name) {
        errno = EINVAL;
        return -1;
    }
    attr_ctx_t *ctx = getctx (h);
    if (!ctx)
        return -1;
    if (val)
        rc = attr_set_rpc (ctx, name, val);
    else
        rc = attr_rm_rpc (ctx, name);
    return rc;
}

int attr_set_cacheonly (flux_t *h, const char *name, const char *val)
{
    attr_ctx_t *ctx;
    attr_t *attr;

    if (!h || !name) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx = getctx (h)))
        return -1;
    if (val) {
        if (!(attr = attr_create (val, FLUX_ATTRFLAG_IMMUTABLE)))
            return -1;
        zhash_update (ctx->hash, name, attr);
        zhash_freefn (ctx->hash, name, attr_destroy);
    }
    else {
        zhash_delete (ctx->hash, name);
    }
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
