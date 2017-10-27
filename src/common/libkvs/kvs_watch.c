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
#include <jansson.h>
#include <flux/core.h>
#include <czmq.h>

#include "treeobj.h"
#include "kvs_dir_private.h"

typedef enum {
    WATCH_JSONSTR, WATCH_DIR,
} watch_type_t;

typedef struct {
    watch_type_t type;
    void *set;
    void *arg;
    flux_t *h;
    char *key;
    uint32_t matchtag;
    flux_future_t *f;
} kvs_watcher_t;

typedef struct {
    zhash_t *watchers; /* kvs_watch_t hashed by stringified matchtag */
    flux_msg_handler_t *w;
} kvs_watch_ctx_t;

static void watch_response_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg);
static int decode_val_object (json_t *val, char **json_str);

static void freectx (kvs_watch_ctx_t *ctx)
{
    if (ctx) {
        zhash_destroy (&ctx->watchers);
        flux_msg_handler_destroy (ctx->w);
        free (ctx);
    }
}

static kvs_watch_ctx_t *getctx (flux_t *h, bool create)
{
    const char *auxkey = "flux::kvs_watch";
    kvs_watch_ctx_t *ctx = (kvs_watch_ctx_t *)flux_aux_get (h, auxkey);
    struct flux_match match = FLUX_MATCH_RESPONSE;

    if (!ctx && create) {
        if (!(ctx = calloc (1, sizeof (*ctx))))
            goto nomem;
        if (!(ctx->watchers = zhash_new ()))
            goto nomem;
        match.topic_glob = "kvs.watch";
        if (!(ctx->w = flux_msg_handler_create (h, match, watch_response_cb,
                                                                ctx)))
            goto nomem;
        flux_aux_set (h, auxkey, ctx, (flux_free_f)freectx);
    }
    return ctx;
nomem:
    freectx (ctx);
    errno = ENOMEM;
    return NULL;
}

static void destroy_watcher (kvs_watcher_t *wp)
{
    if (wp) {
        free (wp->key);
        flux_future_destroy (wp->f); // freees matchtag
        free (wp);
    }
}

static kvs_watcher_t *add_watcher (flux_t *h, const char *key,
                                   watch_type_t type,
                                   uint32_t matchtag, flux_future_t *f,
                                   void *fun, void *arg)
{
    kvs_watcher_t *wp = calloc (1, sizeof (*wp));
    if (!wp) {
        errno = ENOMEM;
        return NULL;
    }
    assert (matchtag != FLUX_MATCHTAG_NONE);
    wp->h = h;
    if (!(wp->key = strdup (key))) {
        free (wp);
        errno = ENOMEM;
        return NULL;
    }
    wp->matchtag = matchtag;
    wp->set = fun;
    wp->type = type;
    wp->arg = arg;
    wp->f = f;

    kvs_watch_ctx_t *ctx = getctx (h, true);
    if (!ctx)
        return NULL;
    int lastcount = zhash_size (ctx->watchers);
    char k[16];
    snprintf (k, sizeof (k), "%"PRIu32, matchtag);
    zhash_update (ctx->watchers, k, wp);
    zhash_freefn (ctx->watchers, k, (zhash_free_fn *)destroy_watcher);

    if (lastcount == 0)
        flux_msg_handler_start (ctx->w);
    return wp;
}

static kvs_watcher_t *lookup_watcher (flux_t *h, uint32_t matchtag)
{
    kvs_watch_ctx_t *ctx = getctx (h, false);
    if (!ctx)
        return NULL;
    char k[16];
    snprintf (k, sizeof (k), "%"PRIu32, matchtag);
    return zhash_lookup (ctx->watchers, k);
}

int flux_kvs_unwatch (flux_t *h, const char *key)
{
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(f = flux_rpc_pack (h, "kvs.unwatch", FLUX_NODEID_ANY, 0,
                             "{s:s}", "key", key)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    /* Delete all watchers for the specified key.
     */
    kvs_watch_ctx_t *ctx = getctx (h, false);
    if (ctx) {
        zlist_t *hashkeys = zhash_keys (ctx->watchers);
        char *k = zlist_first (hashkeys);
        while (k) {
            kvs_watcher_t *wp = zhash_lookup (ctx->watchers, k);
            if (wp && !strcmp (wp->key, key))
                zhash_delete (ctx->watchers, k);
            k = zlist_next (hashkeys);
        }
        zlist_destroy (&hashkeys);
        if (zhash_size (ctx->watchers) == 0)
            flux_msg_handler_stop (ctx->w);
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

static int dispatch_watch (flux_t *h, kvs_watcher_t *wp, const char *json_str)
{
    int errnum = (json_str ? 0 : ENOENT);
    int rc = -1;

    switch (wp->type) {
        case WATCH_DIR: {
            kvs_set_dir_f set = wp->set;
            flux_kvsdir_t *dir = NULL;
            if (json_str) {
                if (!(dir = flux_kvsdir_create (h, NULL, wp->key, json_str)))
                    goto done;
            }
            rc = set (wp->key, dir, wp->arg, errnum);
            if (dir)
                flux_kvsdir_destroy (dir);
            break;
        }
        case WATCH_JSONSTR: {
            kvs_set_f set = wp->set;
            rc = set (wp->key, json_str, wp->arg, errnum);
            break;
        }
    }
done:
    return rc;
}

static void watch_response_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    char *json_str = NULL;
    json_t *val = NULL;
    uint32_t matchtag;
    kvs_watcher_t *wp;

    if (flux_msg_get_matchtag (msg, &matchtag) < 0)
        goto done;
    if (!(wp = lookup_watcher (h, matchtag)))
        goto done;
    if (flux_response_decode (msg, NULL, NULL) < 0) // handle error response
        goto done;
    if (flux_msg_unpack (msg, "{s:o}", "val", &val) < 0)
        goto done;
    if (decode_val_object (val, &json_str) < 0)
        goto done;
    if (dispatch_watch (h, wp, json_str) < 0)
        flux_reactor_stop_error (flux_get_reactor (h));
done:
    free (json_str);
}

static flux_future_t *kvs_watch_rpc (flux_t *h, const char *key,
                                     const char *json_str, int flags)
{
    flux_future_t *f;
    json_t *val = NULL;
    int saved_errno;

    if (!json_str)
        json_str = "null";
    if (!(val = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
        errno = EINVAL;
        goto error;
    }
    if (!(f = flux_rpc_pack (h, "kvs.watch", FLUX_NODEID_ANY, 0,
                             "{s:s s:i s:o}",
                             "key", key,
                             "flags", flags,
                             "val", val))) {
        goto error;
    }
    return f;
error:
    saved_errno = errno;
    json_decref (val);
    errno = saved_errno;
    return NULL;
}

/* val will be one of three things:
 * 1) JSON_NULL, set json_str to string-encoded object (NULL)
 * 2) RFC 11 dir object, set json_str to string-encoded object
 * 3) RFC 11 val object, unbase64, set json_str
 * The caller must free returned json_str (if any)
 */
static int decode_val_object (json_t *val, char **json_str)
{
    char *s;
    int len;

    assert(json_str != NULL);

    if (json_typeof (val) == JSON_NULL) {
        s = NULL;
    }
    else if (treeobj_is_dir (val)) {
        if (treeobj_validate (val) < 0)
            goto error;
        if (!(s = json_dumps (val, JSON_ENCODE_ANY))) {
            errno = EPROTO;
            goto error;
        }
    }
    else if (treeobj_is_val (val)) {
        if (treeobj_validate (val) < 0)
            goto error;
        if (treeobj_decode_val (val, (void **)&s, &len) < 0)
            goto error;
    }
    else {
        errno = EPROTO;
        goto error;
    }
    *json_str = s;
    return 0;
error:
    return -1;
}

static int kvs_watch_rpc_get (flux_future_t *f, char **json_str)
{
    json_t *val;

    if (flux_rpc_get_unpack (f, "{s:o}", "val", &val) < 0)
        goto error;
    if (decode_val_object (val, json_str) < 0)
        goto error;
    return 0;
error:
    return -1;
}

static int kvs_watch_rpc_get_matchtag (flux_future_t *f, uint32_t *matchtag)
{
    uint32_t tag;
    const flux_msg_t *msg;

    if (flux_future_get (f, &msg) < 0)
        return -1;
    if (flux_msg_get_matchtag (msg, &tag) < 0)
        return -1;
    if (matchtag)
        *matchtag = tag;
    return 0;
}

/* N.B. somewhat more complicated than it should be (temporarily):
 * val_in may be NULL, in which case we send a json NULL value in the RPC.
 * Or val_in is an encoded JSON value, so enclose it in an RFC 11 'val' object.
 */
int flux_kvs_watch_once (flux_t *h, const char *key, char **valp)
{
    char *val_in = NULL;
    char *val_out;
    flux_future_t *f = NULL;
    int rc = -1;
    json_t *xval_obj = NULL; // the RFC 11 'val' object
    char *xval_str = NULL;   // the RFC 11 'val' object, encoded as a string
    int saved_errno;

    if (!h || !key || !valp) {
        errno = EINVAL;
        goto done;
    }
    val_in = *valp;
    if (val_in) {
        if (!(xval_obj = treeobj_create_val (val_in, strlen (val_in))))
            goto done;
        if (!(xval_str = treeobj_encode (xval_obj)))
            goto done;
    }
    if (!(f = kvs_watch_rpc (h, key, xval_str, KVS_WATCH_ONCE)))
        goto done;
    if (kvs_watch_rpc_get (f, &val_out) < 0)
        goto done;
    free (val_in);
    *valp = val_out;
    rc = 0;
done:
    saved_errno = errno;
    free (xval_str);
    json_decref (xval_obj);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

static int watch_once_dir (flux_t *h, const char *key, flux_kvsdir_t **dirp)
{
    char *val_in = NULL;
    char *val_out = NULL;
    flux_kvsdir_t *dir_out = NULL;
    flux_future_t *f = NULL;
    int rc = -1;

    if (!h || !key || !dirp) {
        errno = EINVAL;
        goto done;
    }
    if (*dirp) {
        json_t *dir_in = kvsdir_get_obj (*dirp);
        val_in = treeobj_encode (dir_in);
    }
    if (!(f = kvs_watch_rpc (h, key, val_in,
                             KVS_WATCH_ONCE | FLUX_KVS_READDIR)))
        goto done;
    if (kvs_watch_rpc_get (f, &val_out) < 0)
        goto done;
    if (val_out) {
        if (!(dir_out = flux_kvsdir_create (h, NULL, key, val_out)))
            goto done;
    }
    if (*dirp)
        flux_kvsdir_destroy (*dirp);
    *dirp = dir_out;
    rc = 0;
done:
    free (val_out);
    free (val_in);
    flux_future_destroy (f);
    return rc;
}

int flux_kvs_watch_once_dir (flux_t *h, flux_kvsdir_t **dirp,
                             const char *fmt, ...)
{
    va_list ap;
    char *key;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&key, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = ENOMEM;
        return -1;
    }
    rc = watch_once_dir (h, key, dirp);
    free (key);
    return rc;
}

int flux_kvs_watch (flux_t *h, const char *key, kvs_set_f set, void *arg)
{
    flux_future_t *f;
    uint32_t matchtag;
    kvs_watcher_t *wp;
    char *json_str = NULL;

    if (!(f = kvs_watch_rpc (h, key, NULL, KVS_WATCH_FIRST)))
        goto error;
    if (kvs_watch_rpc_get (f, &json_str) < 0)
        goto error;
    if (kvs_watch_rpc_get_matchtag (f, &matchtag) < 0)
        goto error;
    if (!(wp = add_watcher (h, key, WATCH_JSONSTR, matchtag, f, set, arg)))
        goto error;
    dispatch_watch (h, wp, json_str);
    free (json_str);
    return 0;
error:
    free (json_str);
    flux_future_destroy (f);
    return -1;
}

static int watch_dir (flux_t *h, const char *key, kvs_set_dir_f set, void *arg)
{
    flux_future_t *f;
    uint32_t matchtag;
    kvs_watcher_t *wp;
    char *json_str = NULL;

    if (!(f = kvs_watch_rpc (h, key, NULL, KVS_WATCH_FIRST | FLUX_KVS_READDIR)))
        goto error;
    if (kvs_watch_rpc_get (f, &json_str) < 0)
        goto error;
    if (kvs_watch_rpc_get_matchtag (f, &matchtag) < 0)
        goto error;
    if (!(wp = add_watcher (h, key, WATCH_DIR, matchtag, f, set, arg)))
        goto error;
    dispatch_watch (h, wp, json_str);
    free (json_str);
    return 0;
error:
    free (json_str);
    flux_future_destroy (f);
    return -1;
}

int flux_kvs_watch_dir (flux_t *h, kvs_set_dir_f set, void *arg,
                        const char *fmt, ...)
{
    va_list ap;
    char *key;
    int rc;

    va_start (ap, fmt);
    rc = vasprintf (&key, fmt, ap);
    va_end (ap);
    if (rc < 0) {
        errno = ENOMEM;
        return -1;
    }
    rc = watch_dir (h, key, set, arg);
    free (key);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
