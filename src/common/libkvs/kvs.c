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

#include "kvs_deprecated.h"
#include "proto.h"
#include "json_dirent.h"


struct kvsdir_struct {
    flux_t *handle;
    json_object *rootref; /* optional snapshot reference */
    char *key;
    json_object *o;
    int count;
    int usecount;
};

struct kvsdir_iterator_struct {
    kvsdir_t *dir;
    struct json_object_iterator next;
    struct json_object_iterator end;
};

typedef enum {
    WATCH_STRING, WATCH_INT, WATCH_INT64, WATCH_DOUBLE,
    WATCH_BOOLEAN, WATCH_JSONSTR, WATCH_DIR,
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
    json_object *ops;   /* JSON array of put, unlink, etc operations */
    zhash_t *fence_ops;
    json_object *fence_context;
} kvsctx_t;

static void watch_response_cb (flux_t *h, flux_msg_handler_t *w,
                               const flux_msg_t *msg, void *arg);

static void freectx (void *arg)
{
    kvsctx_t *ctx = arg;
    if (ctx) {
        zhash_destroy (&ctx->watchers);
        zhash_destroy (&ctx->fence_ops);
        if (ctx->w)
            flux_msg_handler_destroy (ctx->w);
        Jput (ctx->ops);
        free (ctx);
    }
}

static kvsctx_t *getctx (flux_t *h)
{
    kvsctx_t *ctx = (kvsctx_t *)flux_aux_get (h, "kvscli");
    struct flux_match match = FLUX_MATCH_RESPONSE;

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->watchers = zhash_new ()))
            oom ();
        match.topic_glob = "kvs.watch";
        if (!(ctx->w = flux_msg_handler_create (h, match, watch_response_cb,
                                                                ctx)))
            oom ();
        flux_aux_set (h, "kvscli", ctx, freectx);
    }
    return ctx;
}

/**
 ** kvsdir_t primary functions.
 ** A kvsdir_t is analagous to posix (DIR *).
 **/

void kvsdir_incref (kvsdir_t *dir)
{
    dir->usecount++;
}

void kvsdir_destroy (kvsdir_t *dir)
{
    if (--dir->usecount == 0) {
        Jput (dir->rootref);
        free (dir->key);
        json_object_put (dir->o);
        free (dir);
    }
}

static kvsdir_t *kvsdir_alloc (flux_t *handle, json_object *rootref,
                               const char *key, json_object *o)
{
    kvsdir_t *dir = xzmalloc (sizeof (*dir));

    dir->handle = handle;
    dir->rootref = Jget (rootref); /* may be NULL */
    dir->key = xstrdup (key);
    dir->o = o;
    json_object_get (dir->o);
    dir->count = -1; /* uninitialized */
    dir->usecount = 1;

    return dir;
}

int kvsdir_get_size (kvsdir_t *dir)
{
    json_object_iter iter;

    if (dir->count == -1) {
        dir->count = 0;
        json_object_object_foreachC (dir->o, iter) {
            dir->count++;
        }
    }
    return dir->count;
}

const char *kvsdir_key (kvsdir_t *dir)
{
    return dir->key;
}

void *kvsdir_handle (kvsdir_t *dir)
{
    return dir->handle;
}

void kvsitr_rewind (kvsitr_t *itr)
{
    itr->next = json_object_iter_begin (itr->dir->o);
}

kvsitr_t *kvsitr_create (kvsdir_t *dir)
{
    kvsitr_t *itr = xzmalloc (sizeof (*itr));

    itr->dir = dir;
    itr->next = json_object_iter_begin (itr->dir->o);
    itr->end = json_object_iter_end (itr->dir->o);

    return itr;
}

void kvsitr_destroy (kvsitr_t *itr)
{
    free (itr);
}

const char *kvsitr_next (kvsitr_t *itr)
{
    const char *name = NULL;

    if (!json_object_iter_equal (&itr->end, &itr->next)) {
        name = json_object_iter_peek_name (&itr->next);
        (void)json_object_iter_next (&itr->next);
    }

    return name;
}

bool kvsdir_exists (kvsdir_t *dir, const char *name)
{
    json_object *dirent;
    return (json_object_object_get_ex (dir->o, name, &dirent) && dirent);
}

bool kvsdir_isdir (kvsdir_t *dir, const char *name)
{
    json_object *dirent;
    return (json_object_object_get_ex (dir->o, name, &dirent)
        && dirent
        && (json_object_object_get_ex (dirent, "DIRREF", NULL)
         || json_object_object_get_ex (dirent, "DIRVAL", NULL))
    );
}

bool kvsdir_issymlink (kvsdir_t *dir, const char *name)
{
    json_object *dirent;
    return (json_object_object_get_ex (dir->o, name, &dirent)
        && dirent
        && json_object_object_get_ex (dirent, "LINKVAL", NULL));
}


char *kvsdir_key_at (kvsdir_t *dir, const char *name)
{
    if (!strcmp (dir->key, "."))
        return xstrdup (name);
    return xasprintf ("%s.%s", dir->key, name);
}

/**
 ** GET
 **/

static int getobj (flux_t *h, json_object *rootdir, const char *key,
                   int flags, json_object **val)
{
    flux_future_t *f = NULL;
    const char *json_str;
    json_object *in = NULL;
    json_object *out = NULL;
    json_object *v = NULL;
    int saved_errno;
    int rc = -1;

    if (!h || !key) {
        errno = EINVAL;
        goto done;
    }
    if (!(in = kp_tget_enc (rootdir, key, flags)))
        goto done;
    if (!(f = flux_rpc (h, "kvs.get", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (f, &json_str) < 0)
        goto done;
    if (!json_str || !(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rget_dec (out, NULL, &v) < 0)
        goto done;
    if (val)
        *val = Jget (v);
    rc = 0;
done:
    saved_errno = errno;
    Jput (in);
    Jput (out);
    flux_future_destroy (f);
    errno = saved_errno;
    return rc;
}

int kvs_get (flux_t *h, const char *key, char **val)
{
    json_object *v = NULL;

    if (getobj (h, NULL, key, 0, &v) < 0)
        return -1;
    if (val)
        *val = xstrdup (Jtostr (v));
    Jput (v);
    return 0;
}

int kvs_getat (flux_t *h, const char *treeobj,
               const char *key, char **val)
{
    json_object *v = NULL;
    json_object *dirent = NULL;

    if (!treeobj || !key || !(dirent = Jfromstr (treeobj))
                         || dirent_validate (dirent) < 0) {
        errno = EINVAL;
        goto error;
    }
    if (getobj (h, dirent, key, 0, &v) < 0)
        goto error;
    if (val)
        *val = xstrdup (Jtostr (v));
    Jput (dirent);
    return 0;
error:
    Jput (v);
    Jput (dirent);
    return -1;
}

int kvs_get_dirat (flux_t *h, const char *treeobj,
                   const char *key, kvsdir_t **dir)
{
    json_object *v = NULL;
    json_object *rootref = NULL;
    int rc = -1;

    if (!treeobj || !key || !dir || !(rootref = Jfromstr (treeobj))
                                 || dirent_validate (rootref) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (getobj (h, rootref, key, KVS_PROTO_READDIR, &v) < 0)
        goto done;
    *dir = kvsdir_alloc (h, rootref, key, v);
    rc = 0;
done:
    Jput (v);
    Jput (rootref);
    return rc;
}

int kvs_get_symlinkat (flux_t *h, const char *treeobj,
                       const char *key, char **val)
{
    json_object *v = NULL;
    json_object *dirent = NULL;
    int rc = -1;

    if (!treeobj || !key || !(dirent = Jfromstr (treeobj))
                         || dirent_validate (dirent) < 0) {
        errno = EINVAL;
        goto done;
    }
    if (getobj (h, dirent, key, KVS_PROTO_READLINK, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = xstrdup (json_object_get_string (v));
    rc = 0;
done:
    Jput (v);
    Jput (dirent);
    return rc;
}

int kvs_get_dir (flux_t *h, kvsdir_t **dir, const char *fmt, ...)
{
    va_list ap;
    char *key = NULL;
    json_object *v = NULL;
    int rc = -1;

    if (!h || !dir || !fmt) {
        errno = EINVAL;
        goto done;
    }
    va_start (ap, fmt);
    key = xvasprintf (fmt, ap);
    va_end (ap);

    /* N.B. python kvs tests use empty string key for some reason.
     * Don't break them for now.
     */
    const char *k = strlen (key) > 0 ? key : ".";
    if (getobj (h, NULL, k, KVS_PROTO_READDIR, &v) < 0)
        goto done;
    *dir = kvsdir_alloc (h, NULL, k, v);
    rc = 0;
done:
    Jput (v);
    if (key)
        free (key);
    return rc;
}

int kvs_get_symlink (flux_t *h, const char *key, char **val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, KVS_PROTO_READLINK, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = xstrdup (json_object_get_string (v));
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_treeobj (flux_t *h, const char *key, char **val)
{
    json_object *v = NULL;
    const char *s;
    int rc = -1;

    if (getobj (h, NULL, key, KVS_PROTO_TREEOBJ, &v) < 0)
        goto done;
    if (val) {
        s = json_object_to_json_string_ext (v, JSON_C_TO_STRING_PLAIN);
        *val = xstrdup (s);
    }
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_string (flux_t *h, const char *key, char **val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = xstrdup (json_object_get_string (v));
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_int (flux_t *h, const char *key, int *val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = json_object_get_int (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_int64 (flux_t *h, const char *key, int64_t *val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = json_object_get_int64 (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_double (flux_t *h, const char *key, double *val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_double
        && json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = json_object_get_double (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvs_get_boolean (flux_t *h, const char *key, bool *val)
{
    json_object *v = NULL;
    int rc = -1;

    if (getobj (h, NULL, key, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_boolean) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = json_object_get_boolean (v);
    rc = 0;
done:
    Jput (v);
    return rc;
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
    kvsctx_t *ctx = getctx (h);
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
    kvsctx_t *ctx = getctx (h);
    char *k = xasprintf ("%"PRIu32, matchtag);
    kvs_watcher_t *wp = zhash_lookup (ctx->watchers, k);
    free (k);
    return wp;
}

int kvs_unwatch (flux_t *h, const char *key)
{
    kvsctx_t *ctx = getctx (h);
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
        case WATCH_INT64: {
            kvs_set_int64_f set = wp->set;
            int64_t i = val ? json_object_get_int64 (val) : 0;
            rc = set (wp->key, i, wp->arg, errnum);
            break;
        }
        case WATCH_DOUBLE: {
            kvs_set_double_f set = wp->set;
            double d = val ? json_object_get_double (val) : 0;
            rc = set (wp->key, d, wp->arg, errnum);
            break;
        }
        case WATCH_BOOLEAN: {
            kvs_set_boolean_f set = wp->set;
            bool b = val ? json_object_get_boolean (val) : false;
            rc = set (wp->key, b, wp->arg, errnum);
            break;
        }
        case WATCH_DIR: {
            kvs_set_dir_f set = wp->set;
            kvsdir_t *dir = val ? kvsdir_alloc (h, NULL, wp->key, val) : NULL;
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
        val = (*dirp)->o;
        json_object_get (val);
    }
    if (watch_rpc (h, key, &val, KVS_PROTO_ONCE | KVS_PROTO_READDIR, NULL) < 0)
        goto done;
    if (val == NULL) {
        errno = ENOENT;
        goto done;
    }
    if (*dirp)
        kvsdir_destroy (*dirp);
    *dirp = kvsdir_alloc (h, NULL, key, val);
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

int kvs_watch_int64 (flux_t *h, const char *key, kvs_set_int64_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT64, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_double (flux_t *h, const char *key, kvs_set_double_f set,
                      void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_DOUBLE, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_boolean (flux_t *h, const char *key, kvs_set_boolean_f set,
                       void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, 0, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_BOOLEAN, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

/**
 ** PUT
 **/

static int kvs_put_dirent (flux_t *h, const char *key, json_object *dirent)
{
    kvsctx_t *ctx = getctx (h);
    int rc = -1;
    json_object **ops = ctx->fence_context ? &ctx->fence_context : &ctx->ops;

    if (!h || !key) {
        errno = EINVAL;
        goto done;
    }
    dirent_append (ops, key, dirent);
    rc = 0;
done:
    return rc;
}

int kvs_put_treeobj (flux_t *h, const char *key, const char *treeobj)
{
    json_object *dirent;

    if (!treeobj || !(dirent = Jfromstr (treeobj))
                 || dirent_validate (dirent) < 0) {
        errno = EINVAL;
        return -1;
    }
    return kvs_put_dirent (h, key, dirent);
}

/* Append the key and dirent value to the 'ops' array, to be sent
 * with the next commit/fence request.
 */
int kvs_put (flux_t *h, const char *key, const char *json_str)
{
    int rc = -1;
    json_object *val = NULL;

    if (!json_str)
        return kvs_unlink (h, key);

    if (!(val = Jfromstr (json_str))) {
        errno = EINVAL;
        goto done;
    }
    if (kvs_put_dirent (h, key, dirent_create ("FILEVAL", val)) < 0)
        goto done;
    rc = 0;
done:
    Jput (val);
    return rc;
}

int kvs_put_string (flux_t *h, const char *key, const char *val)
{
    json_object *o = NULL;
    int rc = -1;

    if (val && !(o = json_object_new_string (val)))
        oom ();
    if (kvs_put (h, key, Jtostr (o)) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_int (flux_t *h, const char *key, int val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_int (val)))
        oom ();
    if (kvs_put (h, key, Jtostr (o)) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_int64 (flux_t *h, const char *key, int64_t val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_int64 (val)))
        oom ();
    if (kvs_put (h, key, Jtostr (o)) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_double (flux_t *h, const char *key, double val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_double (val)))
        oom ();
    if (kvs_put (h, key, Jtostr (o)) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_put_boolean (flux_t *h, const char *key, bool val)
{
    json_object *o;
    int rc = -1;

    if (!(o = json_object_new_boolean (val)))
        oom ();
    if (kvs_put (h, key, Jtostr (o)) < 0)
        goto done;
    rc = 0;
done:
    if (o)
        json_object_put (o);
    return rc;
}


int kvs_unlink (flux_t *h, const char *key)
{
    return kvs_put_dirent (h, key, NULL);
}

int kvs_symlink (flux_t *h, const char *key, const char *target)
{
    kvsctx_t *ctx = getctx (h);
    json_object *val = NULL;
    json_object **ops = ctx->fence_context ? &ctx->fence_context : &ctx->ops;

    if (!h || !key || !target) {
        errno = EINVAL;
        return -1;
    }
    if (!(val = json_object_new_string (target))) {
        errno = ENOMEM;
        return -1;
    }
    dirent_append (ops, key, dirent_create ("LINKVAL", val));
    Jput (val);
    return 0;
}

int kvs_mkdir (flux_t *h, const char *key)
{
    int rc = -1;
    kvsctx_t *ctx = getctx (h);
    json_object *val = Jnew ();
    json_object **ops = ctx->fence_context ? &ctx->fence_context : &ctx->ops;

    if (!h || !key) {
        errno = EINVAL;
        goto out;
    }
    rc = 0;
    dirent_append (ops, key, dirent_create ("DIRVAL", val));
out:
    Jput (val);
    return rc;
}

/**
 ** Commit/synchronization
 **/

flux_future_t *kvs_commit_begin (flux_t *h, int flags)
{
    zuuid_t *uuid = NULL;
    flux_future_t *f = NULL;
    int saved_errno;

    if (!(uuid = zuuid_new ())) {
        errno = ENOMEM;
        goto done;
    }
    if (!(f = kvs_fence_begin (h, zuuid_str (uuid), 1, flags)))
        goto done;
done:
    saved_errno = errno;
    zuuid_destroy (&uuid);
    errno = saved_errno;
    return f;
}

int kvs_commit_finish (flux_future_t *f)
{
    return flux_future_get (f, NULL);
}

int kvs_commit (flux_t *h, int flags)
{
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(f = kvs_commit_begin (h, flags)))
        goto done;
    if (kvs_commit_finish (f) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

flux_future_t *kvs_fence_begin (flux_t *h, const char *name,
                                int nprocs, int flags)
{
    kvsctx_t *ctx = getctx (h);
    json_object *in = NULL;
    flux_future_t *f = NULL;
    int saved_errno = errno;
    json_object *fence_ops = NULL;

    if (ctx->fence_ops)
        fence_ops = zhash_lookup (ctx->fence_ops, name);
    if (fence_ops) {
        if (!(in = kp_tfence_enc (name, nprocs, flags, fence_ops)))
            goto done;
        zhash_delete (ctx->fence_ops, name);
    } else {
        if (!(in = kp_tfence_enc (name, nprocs, flags, ctx->ops)))
            goto done;
        Jput (ctx->ops);
        ctx->ops = NULL;
    }
    if (!(f = flux_rpc (h, "kvs.fence", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
done:
    saved_errno = errno;
    Jput (in);
    errno = saved_errno;
    return f;
}

int kvs_fence_finish (flux_future_t *f)
{
    return flux_future_get (f, NULL);
}

void kvs_fence_set_context (flux_t *h, const char *name)
{
    kvsctx_t *ctx = getctx (h);

    if (name) {
        if (!ctx->fence_ops && !(ctx->fence_ops = zhash_new ()))
            oom ();
        if (!(ctx->fence_context = zhash_lookup (ctx->fence_ops, name))) {
            ctx->fence_context = Jnew_ar ();
            if (zhash_insert (ctx->fence_ops, name, ctx->fence_context) < 0)
                oom ();
            zhash_freefn (ctx->fence_ops, name, (zhash_free_fn *)Jput);
        }
    } else
        ctx->fence_context = NULL;
}

void kvs_fence_clear_context (flux_t *h)
{
    kvs_fence_set_context (h, NULL);
}

int kvs_fence (flux_t *h, const char *name, int nprocs, int flags)
{
    flux_future_t *f = NULL;
    int rc = -1;

    if (!(f = kvs_fence_begin (h, name, nprocs, flags)))
        goto done;
    if (kvs_fence_finish (f) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_version (flux_t *h, int *versionp)
{
    flux_future_t *f;
    int version;
    int rc = -1;

    if (!(f = flux_rpc (h, "kvs.getroot", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_getf (f, "{ s:i }", "rootseq", &version) < 0)
        goto done;
    if (versionp)
        *versionp = version;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_wait_version (flux_t *h, int version)
{
    flux_future_t *f;
    int ret = -1;

    if (!(f = flux_rpcf (h, "kvs.sync", FLUX_NODEID_ANY, 0, "{ s:i }",
                           "rootseq", version)))
        goto done;
    /* N.B. response contains (rootseq, rootdir) but we don't need it.
     */
    if (flux_future_get (f, NULL) < 0)
        goto done;
    ret = 0;
done:
    flux_future_destroy (f);
    return ret;
}

int kvs_dropcache (flux_t *h)
{
    flux_future_t *f;
    int rc = -1;

    if (!(f = flux_rpc (h, "kvs.dropcache", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}


/**
 ** kvsdir_t convenience functions
 **/

static int dirgetobj (kvsdir_t *dir, const char *name,
                      int flags, json_object **val)
{
    char *key;
    int rc;

    key = kvsdir_key_at (dir, name);
    rc = getobj (dir->handle, dir->rootref, key, flags, val);
    free (key);
    return rc;
}

int kvsdir_get (kvsdir_t *dir, const char *name, char **valp)
{
    json_object *v = NULL;
    if (dirgetobj (dir, name, 0, &v) < 0)
        return -1;
    if (valp)
        *valp = xstrdup (Jtostr (v));
    Jput (v);
    return 0;
}

int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
{
    int rc = -1;
    char *name, *key;
    va_list ap;
    json_object *v = NULL;

    va_start (ap, fmt);
    if (vasprintf (&name, fmt, ap) < 0)
        oom ();
    va_end (ap);

    key = kvsdir_key_at (dir, name);
    if (getobj (dir->handle, dir->rootref, key, KVS_PROTO_READDIR, &v) < 0)
        goto done;
    *dirp = kvsdir_alloc (dir->handle, dir->rootref, key, v);
    rc = 0;
done:
    Jput (v);
    free (key);
    free (name);
    return rc;
}

int kvsdir_get_symlink (kvsdir_t *dir, const char *name, char **valp)
{
    int rc = -1;
    json_object *v = NULL;

    if (dirgetobj (dir, name, KVS_PROTO_READLINK, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = xstrdup (json_object_get_string (v));
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_get_string (kvsdir_t *dir, const char *name, char **valp)
{
    json_object *v = NULL;
    int rc = -1;

    if (dirgetobj (dir, name, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = xstrdup (json_object_get_string (v));
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_get_int (kvsdir_t *dir, const char *name, int *valp)
{
    json_object *v = NULL;
    int rc = -1;

    if (dirgetobj (dir, name, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_get_int64 (kvsdir_t *dir, const char *name, int64_t *valp)
{
    json_object *v = NULL;
    int rc = -1;

    if (dirgetobj (dir, name, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int64 (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_get_double (kvsdir_t *dir, const char *name, double *valp)
{
    json_object *v = NULL;
    int rc = -1;

    if (dirgetobj (dir, name, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_double
        && json_object_get_type (v) != json_type_int) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = json_object_get_double (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_get_boolean (kvsdir_t *dir, const char *name, bool *valp)
{
    json_object *v = NULL;
    int rc = -1;

    if (dirgetobj (dir, name, 0, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_boolean) {
        errno = EPROTO;
        goto done;
    }
    if (valp)
        *valp = json_object_get_boolean (v);
    rc = 0;
done:
    Jput (v);
    return rc;
}

int kvsdir_put (kvsdir_t *dir, const char *name, const char *val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_string (kvsdir_t *dir, const char *name, const char *val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_string (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_int (kvsdir_t *dir, const char *name, int val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_int (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_int64 (kvsdir_t *dir, const char *name, int64_t val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_int64 (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_double (kvsdir_t *dir, const char *name, double val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_double (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_boolean (kvsdir_t *dir, const char *name, bool val)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_boolean (dir->handle, key, val);
    free (key);

    return (rc);
}

int kvsdir_mkdir (kvsdir_t *dir, const char *name)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_mkdir (dir->handle, key);
    free (key);

    return (rc);
}

int kvsdir_symlink (kvsdir_t *dir, const char *name, const char *target)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_symlink (dir->handle, key, target);
    free (key);

    return (rc);
}

int kvsdir_unlink (kvsdir_t *dir, const char *name)
{
    int rc;
    char *key;

    if (dir->rootref) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_unlink (dir->handle, key);
    free (key);

    return (rc);
}

int kvs_copy (flux_t *h, const char *from, const char *to)
{
    json_object *dirent;
    if (getobj (h, NULL, from, KVS_PROTO_TREEOBJ, &dirent) < 0)
        return -1;
    if (kvs_put_dirent (h, to, dirent) < 0) {
        Jput (dirent);
        return -1;
    }
    return 0;
}

int kvs_move (flux_t *h, const char *from, const char *to)
{
    if (kvs_copy (h, from, to) < 0)
        return -1;
    if (kvs_unlink (h, from) < 0)
        return -1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
