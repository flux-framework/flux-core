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
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <flux/core.h>
#include <czmq.h>

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/blobref.h"

#include "proto.h"
#include "json_dirent.h"


typedef enum {
    WATCH_STRING, WATCH_INT,
    WATCH_JSONSTR, WATCH_DIR,
} watch_type_t;

typedef struct {
    watch_type_t type;
    void *set;
    void *arg;
    flux_t *h;
    char *key;
    uint32_t matchtag;
} kvs_watcher_t;

typedef struct {
    zhash_t *watchers; /* kvs_watch_t hashed by stringified matchtag */
    flux_msg_handler_t *w;
} kvs_watch_ctx_t;

static void watch_response_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg);

static void freectx (void *arg)
{
    kvs_watch_ctx_t *ctx = arg;
    if (ctx) {
        zhash_destroy (&ctx->watchers);
        flux_msg_handler_destroy (ctx->w);
        free (ctx);
    }
}

static kvs_watch_ctx_t *getctx (flux_t *h)
{
    const char *auxkey = "flux::kvs_watch";
    kvs_watch_ctx_t *ctx = (kvs_watch_ctx_t *)flux_aux_get (h, auxkey);
    struct flux_match match = FLUX_MATCH_RESPONSE;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->watchers = zhash_new ()))
            oom ();
        match.topic_glob = "kvs.watch";
        if (!(ctx->w = flux_msg_handler_create (h, match, watch_response_cb,
                                                                ctx)))
            oom ();
        flux_aux_set (h, auxkey, ctx, freectx);
    }
    return ctx;
}

/**
 ** WATCH
 **/

static void destroy_watcher (void *arg)
{
    kvs_watcher_t *wp = arg;
    free (wp->key);
    flux_matchtag_free (wp->h, wp->matchtag);
    free (wp);
}

static kvs_watcher_t *add_watcher (flux_t *h, const char *key, watch_type_t type,
                                   uint32_t matchtag, void *fun, void *arg)
{
    kvs_watch_ctx_t *ctx = getctx (h);
    kvs_watcher_t *wp = xzmalloc (sizeof (*wp));
    int lastcount = zhash_size (ctx->watchers);

    assert (matchtag != FLUX_MATCHTAG_NONE);
    wp->h = h;
    wp->key = xstrdup (key);
    wp->matchtag = matchtag;
    wp->set = fun;
    wp->type = type;
    wp->arg = arg;

    char *k = xasprintf ("%"PRIu32, matchtag);
    zhash_update (ctx->watchers, k, wp);
    zhash_freefn (ctx->watchers, k, destroy_watcher);
    free (k);

    if (lastcount == 0)
        flux_msg_handler_start (ctx->w);
    return wp;
}

static kvs_watcher_t *lookup_watcher (flux_t *h, uint32_t matchtag)
{
    kvs_watch_ctx_t *ctx = getctx (h);
    char *k = xasprintf ("%"PRIu32, matchtag);
    kvs_watcher_t *wp = zhash_lookup (ctx->watchers, k);
    free (k);
    return wp;
}

int kvs_unwatch (flux_t *h, const char *key)
{
    kvs_watch_ctx_t *ctx = getctx (h);
    flux_future_t *f = NULL;
    json_object *in = NULL;
    int rc = -1;

    if (!(in = kp_tunwatch_enc (key)))
        goto done;
    if (!(f = flux_rpc (h, "kvs.unwatch", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    /* Delete all watchers for the specified key.
     */
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
    rc = 0;
done:
    Jput (in);
    flux_future_destroy (f);
    return rc;
}

static int dispatch_watch (flux_t *h, kvs_watcher_t *wp, json_object *val)
{
    int errnum = val ? 0 : ENOENT;
    int rc = -1;

    switch (wp->type) {
        case WATCH_STRING: {
            kvs_set_string_f set = wp->set;
            const char *s = val ? json_object_get_string (val) : NULL;
            rc = set (wp->key, s, wp->arg, errnum);
            break;
        }
        case WATCH_INT: {
            kvs_set_int_f set = wp->set;
            int i = val ? json_object_get_int (val) : 0;
            rc = set (wp->key, i, wp->arg, errnum);
            break;
        }
        case WATCH_DIR: {
            kvs_set_dir_f set = wp->set;
            kvsdir_t *dir = val ? kvsdir_create (h, NULL, wp->key, Jtostr (val))
                                : NULL;
            rc = set (wp->key, dir, wp->arg, errnum);
            if (dir)
                kvsdir_destroy (dir);
            break;
        }
        case WATCH_JSONSTR: {
            kvs_set_f set = wp->set;
            rc = set (wp->key, Jtostr (val), wp->arg, errnum);
            break;
        }
    }
    return rc;
}

static void watch_response_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg)
{
    const char *json_str;
    json_object *out = NULL;
    json_object *val;
    uint32_t matchtag;
    kvs_watcher_t *wp;

    if (flux_response_decode (msg, NULL, &json_str) < 0)
        goto done;
    if (flux_msg_get_matchtag (msg, &matchtag) < 0)
        goto done;
    if (!json_str || !(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rwatch_dec (out, &val) < 0)
        goto done;
    if ((wp = lookup_watcher (h, matchtag)))
        if (dispatch_watch (h, wp, val) < 0)
            flux_reactor_stop_error (flux_get_reactor (h));
done:
    Jput (out);
}

/* Not strictly an RPC since multiple replies are possible.
 * Send the kvs.watch request and receive the first reply, synchronously.
 * If 'matchtag' is non-NULL return the request's matchtag in it for
 * adding to the watcher state; else retire the matchtag.
 */
static int watch_rpc (flux_t *h, const char *key, json_object **val,
                      int flags, uint32_t *matchtag)
{
    struct flux_match match = { .typemask = FLUX_MSGTYPE_RESPONSE,
                                .topic_glob = NULL };
    json_object *in = NULL;
    json_object *out = NULL;
    const char *json_str;
    flux_msg_t *request_msg = NULL;
    flux_msg_t *response_msg = NULL;
    json_object *v = NULL;
    int ret = -1;

    /* Send the request.
     */
    assert ((flags & KVS_PROTO_ONCE) || matchtag != NULL);
    match.matchtag = flux_matchtag_alloc (h, 0);
    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errno = EAGAIN;
        goto done;
    }
    if (!(flags & KVS_PROTO_ONCE))
        flags |= KVS_PROTO_FIRST;
    if (!(in = kp_twatch_enc (key, *val, flags)))
        goto done;
    if (!(request_msg = flux_request_encode ("kvs.watch", Jtostr (in))))
        goto done;
    if (flux_msg_set_matchtag (request_msg, match.matchtag) < 0)
        goto done;
    if (flux_send (h, request_msg, 0) < 0)
        goto done;
    /* Receive the (first) response.
     */
    if (!(response_msg = flux_recv (h, match, 0)))
        goto done;
    if (flux_response_decode (response_msg, NULL, &json_str) < 0)
        goto done;
    if (!json_str || !(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rwatch_dec (out, &v) < 0) /* v may be NULL (no ENOENT here) */
        goto done;
    *val = Jget (v);
    if (matchtag)
        *matchtag = match.matchtag;
    ret = 0;
done:
    if (match.matchtag != FLUX_MATCHTAG_NONE) {
        if (!matchtag || ret == -1)
            flux_matchtag_free (h, match.matchtag);
    }
    Jput (in);
    Jput (out);
    flux_msg_destroy (request_msg);
    flux_msg_destroy (response_msg);
    return ret;
}

static int watch_once_obj (flux_t *h, const char *key, json_object **valp)
{
    int rc = -1;

    if (watch_rpc (h, key, valp, KVS_PROTO_ONCE, NULL) < 0)
        goto done;
    if (*valp == NULL) {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int kvs_watch_once (flux_t *h, const char *key, char **valp)
{
    json_object *val = NULL;
    int rc = -1;

    if (!h || !key || !valp) {
        errno = EINVAL;
        goto done;
    }
    if (*valp) {
        if (!(val = Jfromstr (*valp))) {
            errno = EINVAL;
            goto done;
        }
    }
    if (watch_once_obj (h, key, &val) < 0)
        goto done;
    if (*valp)
        free (*valp);
    *valp = val ? xstrdup (Jtostr (val)) : NULL;
    rc = 0;
done:
    Jput (val);
    return rc;
}

int kvs_watch_once_int (flux_t *h, const char *key, int *valp)
{
    json_object *val;
    int rc = -1;

    if (!(val = json_object_new_int (*valp)))
        oom ();
    if (watch_rpc (h, key, &val, KVS_PROTO_ONCE, NULL) < 0)
        goto done;
    if (!val) {
        errno = ENOENT;
        goto done;
    }
    *valp = json_object_get_int (val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_once_dir (flux_t *h, kvsdir_t **dirp, const char *fmt, ...)
{
    json_object *val = NULL;
    char *key;
    va_list ap;
    int rc = -1;

    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (*dirp) {
        const char *s;
        if (!(s = kvsdir_tostring (*dirp)))
            goto done;
        if (!(val = Jfromstr (s)))
            goto done;
    }
    if (watch_rpc (h, key, &val, KVS_PROTO_ONCE | KVS_PROTO_READDIR, NULL) < 0)
        goto done;
    if (val == NULL) {
        errno = ENOENT;
        goto done;
    }
    if (*dirp)
        kvsdir_destroy (*dirp);
    *dirp = kvsdir_create (h, NULL, key, Jtostr (val));
    rc = 0;
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    return rc;
}

int kvs_watch (flux_t *h, const char *key, kvs_set_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_JSONSTR, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_dir (flux_t *h, kvs_set_dir_f set, void *arg, const char *fmt, ...)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    char *key;
    int rc = -1;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (watch_rpc (h, key, &val, KVS_PROTO_READDIR, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_DIR, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    return rc;
}

int kvs_watch_string (flux_t *h, const char *key, kvs_set_string_f set,
                      void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_STRING, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_int (flux_t *h, const char *key, kvs_set_int_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
