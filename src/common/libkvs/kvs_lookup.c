/*****************************************************************************\
 *  Copyright (c) 2017 Lawrence Livermore National Security, LLC.  Produced at
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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_dir_private.h"
#include "kvs_lookup.h"
#include "treeobj.h"

struct lookup_ctx {
    int flags;
    flux_t *h;
    char *key;
    char *atref;

    json_t *treeobj;
    char *treeobj_str; // json_dumps of tree object returned from lookup
    void *val_data;    // result of base64 decode of val object data
    int val_len;
    bool val_valid;
    json_t *val_obj;
    flux_kvsdir_t *dir;
};

static const char *auxkey = "flux::lookup_ctx";

static void free_ctx (struct lookup_ctx *ctx)
{
    if (ctx) {
        free (ctx->key);
        free (ctx->atref);
        free (ctx->treeobj_str);
        free (ctx->val_data);
        json_decref (ctx->val_obj);
        flux_kvsdir_destroy (ctx->dir);
        free (ctx);
    }
}

static struct lookup_ctx *alloc_ctx (flux_t *h, int flags, const char *key)
{
    struct lookup_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        goto nomem;
    ctx->h = h;
    ctx->flags = flags;
    if (!(ctx->key = strdup (key)))
        goto nomem;
    return ctx;
nomem:
    free_ctx (ctx);
    errno = ENOMEM;
    return NULL;
}

static int validate_lookup_flags (int flags)
{
    switch (flags) {
        case 0:
        case FLUX_KVS_TREEOBJ:
        case FLUX_KVS_READDIR:
        case FLUX_KVS_READDIR | FLUX_KVS_TREEOBJ:
        case FLUX_KVS_READLINK:
            return 0;
        default:
            return -1;
    }
}

flux_future_t *flux_kvs_lookup (flux_t *h, int flags, const char *key)
{
    struct lookup_ctx *ctx;
    flux_future_t *f;

    if (!h || !key || strlen (key) == 0 || validate_lookup_flags (flags) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = alloc_ctx (h, flags, key)))
        return NULL;
    if (!(f = flux_rpc_pack (h, "kvs.get", FLUX_NODEID_ANY, 0, "{s:s s:i}",
                                                         "key", key,
                                                         "flags", flags))) {
        free_ctx (ctx);
        return NULL;
    }
    if (flux_future_aux_set (f, auxkey, ctx, (flux_free_f)free_ctx) < 0) {
        free_ctx (ctx);
        flux_future_destroy (f);
        return NULL;
    }
    return f;
}

flux_future_t *flux_kvs_lookupat (flux_t *h, int flags, const char *key,
                                  const char *treeobj)
{
    flux_future_t *f;
    json_t *obj = NULL;
    struct lookup_ctx *ctx;

    if (!h || !key || strlen (key) == 0 || validate_lookup_flags (flags) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = alloc_ctx (h, flags, key)))
        return NULL;
    if (!treeobj) {
        if (!(f = flux_kvs_lookup (h, flags, key))) {
            free_ctx (ctx);
            return NULL;
        }
    }
    else {
        if (!(ctx->atref = strdup (treeobj)))
            return NULL;
        if (!(obj = json_loads (treeobj, 0, NULL))) {
            errno = EINVAL;
            return NULL;
        }
        if (!(f = flux_rpc_pack (h, "kvs.get", FLUX_NODEID_ANY, 0,
                                    "{s:s s:i s:O}", "key", key,
                                                     "flags", flags,
                                                     "rootdir", obj))) {
            free_ctx (ctx);
            json_decref (obj);
            return NULL;
        }
    }
    if (flux_future_aux_set (f, auxkey, ctx, (flux_free_f)free_ctx) < 0) {
        free_ctx (ctx);
        json_decref (obj);
        flux_future_destroy (f);
        return NULL;
    }
    json_decref (obj);
    return f;
}

int flux_kvs_lookup_get (flux_future_t *f, const char **json_str)
{
    struct lookup_ctx *ctx;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx->treeobj)) {
        if (flux_rpc_get_unpack (f, "{s:o}", "val", &ctx->treeobj) < 0)
            return -1;
    }
    /* If TREEOBJ or READDIR flags, val is a tree object.
     * Re-encode as a string and return.
     */
    if ((ctx->flags & FLUX_KVS_TREEOBJ) || (ctx->flags & FLUX_KVS_READDIR)) {
        if (!ctx->treeobj_str) {
            size_t flags = JSON_ENCODE_ANY | JSON_COMPACT | JSON_SORT_KEYS;
            if (!(ctx->treeobj_str = json_dumps (ctx->treeobj, flags))) {
                errno = EINVAL;
                return -1;
            }
        }
        if (json_str)
            *json_str = ctx->treeobj_str;
    }
    /* No flags, val is a 'val' object.
     * Decide the data and return it as a string if it is properly terminated.
     */
    else if (ctx->flags == 0) {
        if (!ctx->val_valid) {
            if (treeobj_decode_val (ctx->treeobj, &ctx->val_data,
                                                  &ctx->val_len) < 0)
                return -1;
            ctx->val_valid = true;
            char *s = ctx->val_data;
            if (ctx->val_len < 1 || s[ctx->val_len - 1] != '\0') {
                errno = EINVAL;
                return -1;
            }
            if (json_str)
                *json_str = s;
        }
    }
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    struct lookup_ctx *ctx;
    va_list ap;
    int rc;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return -1;
    }
    if (!ctx->val_obj) {
        const char *json_str;
        if (flux_kvs_lookup_get (f, &json_str) < 0)
            return -1;
        if (!(ctx->val_obj = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
            errno = EINVAL;
            return -1;
        }
    }
    va_start (ap, fmt);
    if ((rc = json_vunpack_ex (ctx->val_obj, NULL, 0, fmt, ap) < 0))
        errno = EINVAL;
    va_end (ap);

    return rc;
}

int flux_kvs_lookup_get_raw (flux_future_t *f, const void **data, int *len)
{
    struct lookup_ctx *ctx;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx->treeobj)) {
        if (flux_rpc_get_unpack (f, "{s:o}", "val", &ctx->treeobj) < 0) {
            errno = EINVAL;
            return -1;
        }
    }
    if (!ctx->val_valid) {
        if (treeobj_decode_val (ctx->treeobj, &ctx->val_data,
                                              &ctx->val_len) < 0)
            return -1;
        ctx->val_valid = true;
    }
    if (data)
        *data = ctx->val_data;
    if (len)
        *len = ctx->val_len;
    return 0;
}

int flux_kvs_lookup_get_dir (flux_future_t *f, const flux_kvsdir_t **dirp)
{
    struct lookup_ctx *ctx;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx->treeobj)) {
        if (flux_rpc_get_unpack (f, "{s:o}", "val", &ctx->treeobj) < 0)
            return -1;
    }
    if (!ctx->dir) {
        if (!(ctx->dir = kvsdir_create_fromobj (ctx->h, ctx->atref,
                                                ctx->key, ctx->treeobj)))
            return -1;
    }
    if (dirp)
        *dirp = ctx->dir;
    return 0;
}

int flux_kvs_lookup_get_symlink (flux_future_t *f, const char **target)
{
    struct lookup_ctx *ctx;
    json_t *str;
    const char *s;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return -1;
    }
    if (!(ctx->treeobj)) {
        if (flux_rpc_get_unpack (f, "{s:o}", "val", &ctx->treeobj) < 0)
            return -1;
    }
    if (!treeobj_is_symlink (ctx->treeobj)) {
        errno = EINVAL;
        return -1;
    }
    if (!(str = treeobj_get_data (ctx->treeobj))
                                || !(s = json_string_value (str))) {
        errno = EINVAL;
        return -1;
    }
    if (target)
        *target = s;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
