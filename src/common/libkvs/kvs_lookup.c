/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <flux/core.h>

#include "kvs_dir_private.h"
#include "kvs_lookup.h"
#include "kvs_util_private.h"
#include "treeobj.h"

struct lookup_ctx {
    flux_t *h;
    char *key;
    char *atref;
    int flags;

    json_t *treeobj;
    char *treeobj_str; // json_dumps of tree object returned from lookup
    void *val_data;    // result of base64 decode of val object data
    size_t val_len;
    bool val_valid;
    json_t *val_obj;
    flux_kvsdir_t *dir;
};

static const char *auxkey = "flux::lookup_ctx";

#define FLUX_KVS_WATCH_FLAGS (FLUX_KVS_WATCH_FULL \
                              | FLUX_KVS_WATCH_UNIQ \
                              | FLUX_KVS_WATCH_APPEND)

static void free_ctx (struct lookup_ctx *ctx)
{
    if (ctx) {
        free (ctx->key);
        free (ctx->atref);
        json_decref (ctx->treeobj);
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

static int validate_lookup_flags (int flags, bool watch_ok)
{
    if ((flags & FLUX_KVS_WATCH) && !watch_ok)
        return -1;
    if ((flags & FLUX_KVS_WATCH_FLAGS)
        && !(flags & FLUX_KVS_WATCH))
        return -1;
    /* FLUX_KVS_WAITCREATE does not require FLUX_KVS_WATCH to be set,
     * but it requires that we be able to communicate with the
     * kvs-watch module, so we use the watch_ok bool here.
     */
    if ((flags & FLUX_KVS_WAITCREATE) && !watch_ok)
        return -1;

    flags &= ~FLUX_KVS_WATCH;
    flags &= ~(FLUX_KVS_WATCH_FLAGS);

    flags &= ~FLUX_KVS_WAITCREATE;

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

flux_future_t *flux_kvs_lookup (flux_t *h,
                                const char *ns,
                                int flags,
                                const char *key)
{
    struct lookup_ctx *ctx;
    flux_future_t *f;
    const char *topic = "kvs.lookup";
    int rpc_flags = 0;

    if (!h
        || !key
        || strlen (key) == 0
        || validate_lookup_flags (flags, true) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!ns) {
        if (!(ns = kvs_get_namespace ()))
            return NULL;
    }
    if (!(ctx = alloc_ctx (h, flags, key)))
        return NULL;
    if ((flags & FLUX_KVS_WATCH)
        || (flags & FLUX_KVS_WAITCREATE))
        topic = "kvs-watch.lookup"; // redirect to kvs-watch module
    if ((flags & FLUX_KVS_WATCH))
        rpc_flags |= FLUX_RPC_STREAMING;
    if (!(f = flux_rpc_pack (h,
                             topic,
                             FLUX_NODEID_ANY,
                             rpc_flags,
                             "{s:s s:s s:i}",
                             "key", key,
                             "namespace", ns,
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

flux_future_t *flux_kvs_lookupat (flux_t *h,
                                  int flags,
                                  const char *key,
                                  const char *treeobj)
{
    flux_future_t *f;
    json_t *obj = NULL;
    struct lookup_ctx *ctx;

    /* N.B. FLUX_KVS_WATCH is not valid for lookupat (r/o snapshot).
     */
    if (!h
        || !key
        || strlen (key) == 0
        || !treeobj
        || validate_lookup_flags (flags, false) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = alloc_ctx (h, flags, key)))
        return NULL;
    if (!(ctx->atref = strdup (treeobj)))
        return NULL;
    if (!(obj = json_loads (treeobj, 0, NULL))) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f = flux_rpc_pack (h,
                             "kvs.lookup",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i s:O}",
                             "key", key,
                             "flags", flags,
                             "rootdir", obj))) {
        free_ctx (ctx);
        json_decref (obj);
        return NULL;
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

static int decode_treeobj (flux_future_t *f, json_t **treeobj)
{
    json_t *obj;

    if (flux_rpc_get_unpack (f, "{s:o}", "val", &obj) < 0)
        return -1;
    if (treeobj_validate (obj) < 0) {
        errno = EPROTO;
        return -1;
    }
    *treeobj = obj;
    return 0;
}

static struct lookup_ctx *get_lookup_ctx (flux_future_t *f)
{
    struct lookup_ctx *ctx;

    if (!(ctx = flux_future_aux_get (f, auxkey))) {
        errno = EINVAL;
        return NULL;
    }
    return ctx;
}

/* Parse the lookup response message, extracting the 'val' treeobj.
 * If decoded results were previously cached and the response has
 * changed (e.g. future has been reset and another response has arrived),
 * invalidate the cached results.
 */
static int parse_response (flux_future_t *f, struct lookup_ctx *ctx)
{
    json_t *treeobj2;

    if (decode_treeobj (f, &treeobj2) < 0)
        return -1;
    if (!ctx->treeobj || !json_equal (ctx->treeobj, treeobj2)) {
        json_decref (ctx->treeobj);
        ctx->treeobj = json_incref (treeobj2);
        if (ctx->treeobj_str) {
            free (ctx->treeobj_str);
            ctx->treeobj_str = NULL;
        }
        if (ctx->val_valid) {
            free (ctx->val_data);
            ctx->val_data = NULL;
            ctx->val_valid = false;
        }
        if (ctx->val_obj) {
            json_decref (ctx->val_obj);
            ctx->val_obj = NULL;
        }
        if (ctx->dir) {
            flux_kvsdir_destroy (ctx->dir);
            ctx->dir = NULL;
        }
    }
    return 0;
}

int flux_kvs_lookup_get (flux_future_t *f, const char **value)
{
    struct lookup_ctx *ctx;

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!ctx->val_valid) {
        if (treeobj_decode_val (ctx->treeobj,
                                &ctx->val_data,
                                &ctx->val_len) < 0)
            return -1;
        ctx->val_valid = true;
        // N.B. val_data includes xtra 0 byte term not reflected in val_len
    }
    if (value)
        *value = ctx->val_data;
    return 0;
}

int flux_kvs_lookup_get_treeobj (flux_future_t *f, const char **treeobj)
{
    struct lookup_ctx *ctx;

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!ctx->treeobj_str) {
        if (!(ctx->treeobj_str = treeobj_encode (ctx->treeobj)))
            return -1;
    }
    if (treeobj)
        *treeobj= ctx->treeobj_str;
    return 0;
}

int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    struct lookup_ctx *ctx;
    va_list ap;
    int rc;

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!ctx->val_valid) {
        if (treeobj_decode_val (ctx->treeobj,
                                &ctx->val_data,
                                &ctx->val_len) < 0)
            return -1;
        ctx->val_valid = true;
    }
    if (!ctx->val_obj) {
        if (!(ctx->val_obj = json_loadb (ctx->val_data,
                                         ctx->val_len,
                                         JSON_DECODE_ANY,
                                         NULL))) {
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

int flux_kvs_lookup_get_raw (flux_future_t *f, const void **data, size_t *len)
{
    struct lookup_ctx *ctx;

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!ctx->val_valid) {
        if (treeobj_decode_val (ctx->treeobj,
                                &ctx->val_data,
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

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!ctx->dir) {
        if (!(ctx->dir = kvsdir_create_fromobj (ctx->h,
                                                ctx->atref,
                                                ctx->key,
                                                ctx->treeobj)))
            return -1;
    }
    if (dirp)
        *dirp = ctx->dir;
    return 0;
}

int flux_kvs_lookup_get_symlink (flux_future_t *f,
                                 const char **ns,
                                 const char **target)
{
    struct lookup_ctx *ctx;
    const char *n = NULL, *t = NULL;

    if (!(ctx = get_lookup_ctx (f)))
        return -1;
    if (parse_response (f, ctx) < 0)
        return -1;
    if (!treeobj_is_symlink (ctx->treeobj)) {
        errno = EINVAL;
        return -1;
    }
    if (treeobj_get_symlink (ctx->treeobj, &n, &t) < 0)
        return -1;
    if (ns)
        *ns = n;
    if (target)
        *target = t;
    return 0;
}

const char *flux_kvs_lookup_get_key (flux_future_t *f)
{
    struct lookup_ctx *ctx;

    if (!(ctx = get_lookup_ctx (f)))
        return NULL;
    return ctx->key;
}


/* This only applies with FLUX_KVS_WATCH.
 * Causes a stream of lookup responses to end with an ENODATA response.
 */
int flux_kvs_lookup_cancel (flux_future_t *f)
{
    struct lookup_ctx *ctx;
    flux_future_t *f2;

    if (!f
        || !(ctx = flux_future_aux_get (f, auxkey))
        || (!(ctx->flags & FLUX_KVS_WATCH)
            && !(ctx->flags & FLUX_KVS_WAITCREATE))) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              "kvs-watch.cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        return -1;
    flux_future_destroy (f2);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
