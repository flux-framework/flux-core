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
#include <flux/core.h>

#include "proto.h"

#include "src/common/libutil/shortjson.h"
#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"

struct kvsdir_struct {
    flux_t handle;
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
    WATCH_BOOLEAN, WATCH_OBJECT, WATCH_JSONSTR, WATCH_DIR,
} watch_type_t;

typedef struct {
    watch_type_t type;
    void *set;
    void *arg;
    flux_t h;
    char *key;
    uint32_t matchtag;
} kvs_watcher_t;

typedef struct {
    zhash_t *watchers; /* kvs_watch_t hashed by stringified matchtag */
    char *cwd;
    zlist_t *dirstack;
} kvsctx_t;

static int watch_rep_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg);

static void freectx (void *arg)
{
    kvsctx_t *ctx = arg;
    zhash_destroy (&ctx->watchers);
    zlist_destroy (&ctx->dirstack);
    free (ctx->cwd);
    free (ctx);
}

static kvsctx_t *getctx (flux_t h)
{
    kvsctx_t *ctx = (kvsctx_t *)flux_aux_get (h, "kvscli");

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        if (!(ctx->watchers = zhash_new ()))
            oom ();
        if (!(ctx->dirstack = zlist_new ()))
            oom ();
        if (!(ctx->cwd = xstrdup (".")))
            oom ();
        flux_aux_set (h, "kvscli", ctx, freectx);
    }
    return ctx;
}

/**
 ** Current working directory implementation is just used internally
 ** for now.  I'm not sure it is all that useful of an abstractions
 ** to expose in the KVS API.
 **/

/* Create new path from current working directory and relative path.
 * Confusing: "." is our path separator, so think of it as POSIX "/",
 * and there is no equiv of POSIX "." and "..".
 */
static char *pathcat (const char *cwd, const char *relpath)
{
    char *path;
    bool fq = false;

    while (*cwd == '.')
        cwd++;
    while (*relpath == '.') {
        relpath++;
        fq = true;
    }
    if (fq || strlen (cwd) == 0)
        path = xstrdup (strlen (relpath) > 0 ? relpath : ".");
    else
        path = xasprintf ("%s.%s", cwd, relpath);
    return path;
}

const char *kvs_getcwd (flux_t h)
{
    kvsctx_t *ctx = getctx (h);
    return ctx->cwd;
}

#if 0
static void kvs_chdir (flux_t h, const char *path)
{
    kvsctx_t *ctx = getctx (h);
    char *new;

    new = pathcat (ctx->cwd, xstrdup (path ? path : "."));
    free (ctx->cwd);
    ctx->cwd = new;
}
#endif
static void kvs_pushd (flux_t h, const char *path)
{
    kvsctx_t *ctx = getctx (h);

    if (zlist_push (ctx->dirstack, ctx->cwd) < 0)
        oom ();
    ctx->cwd = pathcat (ctx->cwd, path ? path : ".");
}

static void kvs_popd (flux_t h)
{
    kvsctx_t *ctx = getctx (h);

    if (zlist_size (ctx->dirstack) > 0) {
        free (ctx->cwd);
        ctx->cwd = zlist_pop (ctx->dirstack);
    }
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
        free (dir->key);
        json_object_put (dir->o);
        free (dir);
    }
}

static kvsdir_t *kvsdir_alloc (flux_t handle, const char *key, json_object *o)
{
    kvsdir_t *dir = xzmalloc (sizeof (*dir));

    dir->handle = handle;
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
    char *key;

    if (!strcmp (dir->key, ".") != 0)
        key = xstrdup (name);
    else if (asprintf (&key, "%s.%s", dir->key, name) < 0)
        oom ();
    return key;
}

/**
 ** GET
 **/

int kvs_get (flux_t h, const char *key, char **val)
{
    flux_rpc_t *rpc = NULL;
    const char *json_str;
    char *k = NULL;
    JSON in = NULL;
    JSON out = NULL;
    JSON v;
    int saved_errno;
    int rc = -1;

    if (!h || !key) {
        errno = EINVAL;
        goto done;
    }
    k = pathcat (kvs_getcwd (h), key);
    if (!(in = kp_tget_enc (k, false, false)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.get", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    /* translates { "key": null } response into failure with errno = ENOENT */
    if (kp_rget_dec (out, &v) < 0)
        goto done;
    assert (v != NULL);
    if (val)
        *val = xstrdup (Jtostr (v));
    rc = 0;
done:
    saved_errno = errno;
    Jput (in);
    Jput (out);
    if (k)
        free (k);
    flux_rpc_destroy (rpc);
    errno = saved_errno;
    return rc;
}

int kvs_get_obj (flux_t h, const char *key, JSON *val)
{
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (val)
        *val = o;
    else
        Jput (o);
    rc = 0;
done:
    if (json_str)
        free (json_str);
    return rc;
}

int kvs_get_dir (flux_t h, kvsdir_t **dir, const char *fmt, ...)
{
    va_list ap;
    char *key = NULL;
    char *k = NULL;
    const char *json_str;
    flux_rpc_t *rpc = NULL;
    JSON in = NULL;
    JSON out = NULL;
    JSON v;
    int rc = -1;

    if (!h || !dir || !fmt) {
        errno = EINVAL;
        goto done;
    }
    va_start (ap, fmt);
    key = xvasprintf (fmt, ap);
    va_end (ap);
    k = pathcat (kvs_getcwd (h), key);
    if (!(in = kp_tget_enc (k, true, false)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.get", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rget_dec (out, &v) < 0)
        goto done;
    *dir = kvsdir_alloc (h, k, v);
    rc = 0;
done:
    Jput (in);
    Jput (out);
    if (k)
        free (k);
    if (key)
        free (key);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_get_symlink (flux_t h, const char *key, char **val)
{
    char *k = NULL;
    flux_rpc_t *rpc = NULL;
    const char *json_str;
    JSON in = NULL;
    JSON out = NULL;
    JSON v;
    const char *s;
    int rc = -1;

    if (!h || !key || !val) {
        errno = EINVAL;
        goto done;
    }
    k = pathcat (kvs_getcwd (h), key);
    if (!(in = kp_tget_enc (k, false, true)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.get", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rget_dec (out, &v) < 0)
        goto done;
    if (json_object_get_type (v) != json_type_string
                            || !(s = json_object_get_string (v))) {
        errno = EPROTO;
        goto done;
    }
    *val = xstrdup (s);
    rc = 0;
done:
    Jput (in);
    Jput (out);
    if (k)
        free (k);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_get_string (flux_t h, const char *key, char **valp)
{
    char *json_str = NULL;
    json_object *o = NULL;
    const char *s;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    s = json_object_get_string (o);
    if (valp)
        *valp = xstrdup (s);
    rc = 0;
done:
    Jput (o);
    if (json_str)
        free (json_str);
    return rc;
}

int kvs_get_int (flux_t h, const char *key, int *valp)
{
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int (o);
    rc = 0;
done:
    Jput (o);
    if (json_str)
        free (json_str);
    return rc;
}

int kvs_get_int64 (flux_t h, const char *key, int64_t *valp)
{
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int64 (o);
    rc = 0;
done:
    Jput (o);
    if (json_str)
        free (json_str);
    return rc;
}

int kvs_get_double (flux_t h, const char *key, double *valp)
{
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (json_object_get_type (o) != json_type_double) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_double (o);
    rc = 0;
done:
    Jput (o);
    if (json_str)
        free (json_str);
    return rc;
}

int kvs_get_boolean (flux_t h, const char *key, bool *valp)
{
    char *json_str = NULL;
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &json_str) < 0)
        goto done;
    if (!(o = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (json_object_get_type (o) != json_type_boolean) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_boolean (o);
    rc = 0;
done:
    Jput (o);
    if (json_str)
        free (json_str);
    return rc;
}

/**
 ** WATCH
 **/

static void destroy_watcher (void *arg)
{
    kvs_watcher_t *wp = arg;
    free (wp->key);
    flux_matchtag_free (wp->h, wp->matchtag, 1);
    free (wp);
}

static kvs_watcher_t *add_watcher (flux_t h, const char *key, watch_type_t type,
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

    char *k = xasprintf ("%u", matchtag);
    zhash_update (ctx->watchers, k, wp);
    zhash_freefn (ctx->watchers, k, destroy_watcher);
    free (k);

    if (lastcount == 0)
        (void)flux_msghandler_add (h, FLUX_MSGTYPE_RESPONSE, "kvs.watch",
                                   watch_rep_cb, ctx);
    return wp;
}

static kvs_watcher_t *lookup_watcher (flux_t h, uint32_t matchtag)
{
    kvsctx_t *ctx = getctx (h);
    char *k = xasprintf ("%u", matchtag);
    kvs_watcher_t *wp = zhash_lookup (ctx->watchers, k);
    free (k);
    return wp;
}

int kvs_unwatch (flux_t h, const char *key)
{
    kvsctx_t *ctx = getctx (h);
    flux_rpc_t *rpc = NULL;
    JSON in = NULL;
    int rc = -1;

    if (!(in = kp_tunwatch_enc (key)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.unwatch", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
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
        flux_msghandler_remove (h, FLUX_MSGTYPE_RESPONSE, "kvs.watch");
    rc = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return rc;
}

static int dispatch_watch (flux_t h, kvs_watcher_t *wp, json_object *val)
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
            kvsdir_t *dir = val ? kvsdir_alloc (h, wp->key, val) : NULL;
            rc = set (wp->key, dir, wp->arg, errnum);
            if (dir)
                kvsdir_destroy (dir);
            break;
        }
        case WATCH_OBJECT: {
            kvs_set_obj_f set = wp->set;
            rc = set (wp->key, val, wp->arg, errnum);
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

static int watch_rep_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    const char *json_str;
    JSON out = NULL;
    JSON val;
    uint32_t matchtag;
    kvs_watcher_t *wp;
    int rc = 0;

    if (flux_response_decode (*zmsg, NULL, &json_str) < 0)
        goto done;
    if (flux_msg_get_matchtag (*zmsg, &matchtag) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kp_rwatch_dec (out, &val) < 0)
        goto done;
    if ((wp = lookup_watcher (h, matchtag)))
        rc = dispatch_watch (h, wp, val);
done:
    Jput (out);
    zmsg_destroy (zmsg);
    return rc;
}

/* Not strictly an RPC since multiple replies are possible.
 * Send the kvs.watch request and receive the first reply, synchronously.
 * If 'matchtag' is non-NULL return the request's matchtag in it for
 * adding to the watcher state; else retire the matchtag.
 */
static int watch_rpc (flux_t h, const char *key, JSON *val,
                      bool once, bool directory, uint32_t *matchtag)
{
    struct flux_match match = { .typemask = FLUX_MSGTYPE_RESPONSE, .bsize = 0,
                                .topic_glob = NULL };
    JSON in = NULL;
    JSON out = NULL;
    const char *json_str;
    flux_msg_t *request_msg = NULL;
    flux_msg_t *response_msg = NULL;
    JSON v = NULL;
    int ret = -1;

    /* Send the request.
     */
    assert (once || matchtag != NULL);
    match.matchtag = flux_matchtag_alloc (h, 1);
    if (match.matchtag == FLUX_MATCHTAG_NONE) {
        errno = EAGAIN;
        goto done;
    }
    if (!(in = kp_twatch_enc (key, *val, once,
                              once ? false : true, directory, false)))
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
    if (!(out = Jfromstr (json_str))) {
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
            flux_matchtag_free (h, match.matchtag, 1);
    }
    Jput (in);
    Jput (out);
    flux_msg_destroy (request_msg);
    flux_msg_destroy (response_msg);
    return ret;
}

/* *valp is IN/OUT parameter.
 * IN *valp is freed internally.  Caller must free OUT *val.
 */
int kvs_watch_once_obj (flux_t h, const char *key, json_object **valp)
{
    int rc = -1;

    if (watch_rpc (h, key, valp, true, false, NULL) < 0)
        goto done;
    if (*valp == NULL) {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int kvs_watch_once (flux_t h, const char *key, char **valp)
{
    JSON val = NULL;
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
    if (kvs_watch_once_obj (h, key, &val) < 0)
        goto done;
    if (*valp)
        free (*valp);
    *valp = val ? xstrdup (Jtostr (val)) : NULL;
    rc = 0;
done:
    Jput (val);
    return rc;
}

int kvs_watch_once_int (flux_t h, const char *key, int *valp)
{
    json_object *val;
    int rc = -1;

    if (!(val = json_object_new_int (*valp)))
        oom ();
    if (watch_rpc (h, key, &val, true, false, NULL) < 0)
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

int kvs_watch_once_dir (flux_t h, kvsdir_t **dirp, const char *fmt, ...)
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
    if (watch_rpc (h, key, &val, true, true, NULL) < 0)
        goto done;
    if (val == NULL) {
        errno = ENOENT;
        goto done;
    }
    if (*dirp)
        kvsdir_destroy (*dirp);
    *dirp = kvsdir_alloc (h, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    return rc;
}

int kvs_watch_obj (flux_t h, const char *key, kvs_set_obj_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_OBJECT, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch (flux_t h, const char *key, kvs_set_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_JSONSTR, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_dir (flux_t h, kvs_set_dir_f set, void *arg, const char *fmt, ...)
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

    if (watch_rpc (h, key, &val, false, true, &matchtag) < 0)
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

int kvs_watch_string (flux_t h, const char *key, kvs_set_string_f set,
                      void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_STRING, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_int (flux_t h, const char *key, kvs_set_int_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_int64 (flux_t h, const char *key, kvs_set_int64_f set, void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT64, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_double (flux_t h, const char *key, kvs_set_double_f set,
                      void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_DOUBLE, matchtag, set, arg);
    dispatch_watch (h, wp, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_boolean (flux_t h, const char *key, kvs_set_boolean_f set,
                       void *arg)
{
    uint32_t matchtag;
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (watch_rpc (h, key, &val, false, false, &matchtag) < 0)
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

/* N.B. kvs_put() of a NULL value is the same as kvs_unlink().
 */
int kvs_put (flux_t h, const char *key, const char *json_str)
{
    flux_rpc_t *rpc = NULL;
    JSON in = NULL;
    char *k = NULL;
    int rc = -1;

    if (!h || !key) {
        errno = EINVAL;
        goto done;
    }
    k = pathcat (kvs_getcwd (h), key);
    if (!(in = kp_tput_enc (k, json_str, false, false)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.put", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    if (k)
        free (k);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_put_obj (flux_t h, const char *key, json_object *val)
{
    int rc = -1;

    if (kvs_put (h, key, Jtostr (val)) < 0)
        goto done;
    rc = 0;
done:
    return rc;
}

int kvs_put_string (flux_t h, const char *key, const char *val)
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

int kvs_put_int (flux_t h, const char *key, int val)
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

int kvs_put_int64 (flux_t h, const char *key, int64_t val)
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

int kvs_put_double (flux_t h, const char *key, double val)
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

int kvs_put_boolean (flux_t h, const char *key, bool val)
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


int kvs_unlink (flux_t h, const char *key)
{
    return kvs_put (h, key, NULL);
}

int kvs_symlink (flux_t h, const char *key, const char *target)
{
    flux_rpc_t *rpc = NULL;
    JSON in = NULL;
    JSON val = NULL;
    char *k = NULL;
    int rc = -1;

    if (!h || !key || !target) {
        errno = EINVAL;
        goto done;
    }
    k = pathcat (kvs_getcwd (h), key);
    if (!(val = json_object_new_string (target))) {
        errno = ENOMEM;
        goto done;
    }
    if (!(in = kp_tput_enc (k, Jtostr (val), true, false)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.put", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    Jput (val);
    if (k)
        free (k);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_mkdir (flux_t h, const char *key)
{
    JSON in = NULL;
    char *k = NULL;
    flux_rpc_t *rpc = NULL;
    int rc = -1;

    k = pathcat (kvs_getcwd (h), key);
    if (!(in = kp_tput_enc (k, NULL, false, true)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.put", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    if (k)
        free (k);
    flux_rpc_destroy (rpc);
    return rc;
}

/**
 ** Commit/synchronization
 **/

int kvs_commit (flux_t h)
{
    JSON in = NULL;
    flux_rpc_t *rpc = NULL;
    const char *json_str;
    int rc = -1;

    if (!(in = kp_tcommit_enc (NULL, NULL, NULL, 0)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.commit", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_fence (flux_t h, const char *name, int nprocs)
{
    JSON in = NULL;
    flux_rpc_t *rpc = NULL;
    const char *json_str;
    int rc = -1;

    if (!(in = kp_tcommit_enc (NULL, NULL, name, nprocs)))
        goto done;
    if (!(rpc = flux_rpc (h, "kvs.commit", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    rc = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_get_version (flux_t h, int *versionp)
{
    flux_rpc_t *rpc;
    const char *json_str;
    JSON out = NULL;
    int version;
    int rc = -1;

    if (!(rpc = flux_rpc (h, "kvs.getroot", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str)) || !Jget_int (out, "rootseq", &version)) {
        errno = EPROTO;
        goto done;
    }
    if (versionp)
        *versionp = version;
    rc = 0;
done:
    Jput (out);
    flux_rpc_destroy (rpc);
    return rc;
}

int kvs_wait_version (flux_t h, int version)
{
    flux_rpc_t *rpc;
    const char *json_str;
    JSON in = Jnew ();
    int ret = -1;

    Jadd_int (in, "rootseq", version);
    if (!(rpc = flux_rpc (h, "kvs.sync", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, &json_str) < 0)
        goto done;
    /* N.B. response contains (rootseq, rootdir) but we don't use it.
     */
    ret = 0;
done:
    Jput (in);
    flux_rpc_destroy (rpc);
    return ret;
}

int kvs_dropcache (flux_t h)
{
    flux_rpc_t *rpc;
    int rc = -1;

    if (!(rpc = flux_rpc (h, "kvs.dropcache", NULL, FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL, NULL) < 0)
        goto done;
    rc = 0;
done:
    flux_rpc_destroy (rpc);
    return rc;
}


/**
 ** kvsdir_t convenience functions
 **/

int kvsdir_get_obj (kvsdir_t *dir, const char *name, json_object **valp)
{
    int rc = -1;
    char *json_str = NULL;
    JSON out;

    kvs_pushd (dir->handle, dir->key);
    if (kvs_get (dir->handle, name, &json_str) < 0)
        goto done;
    if (!(out = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    *valp = out;
    rc = 0;
done:
    kvs_popd (dir->handle);
    if (json_str)
        free (json_str);
    return rc;
}

int kvsdir_get (kvsdir_t *dir, const char *name, char **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
{
    int rc;
    char *name;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&name, fmt, ap) < 0)
        oom ();
    va_end (ap);

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_dir (dir->handle, dirp, "%s", name);
    kvs_popd (dir->handle);

    if (name)
        free (name);
    return rc;
}

int kvsdir_get_symlink (kvsdir_t *dir, const char *name, char **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_symlink (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_string (kvsdir_t *dir, const char *name, char **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_string (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_int (kvsdir_t *dir, const char *name, int *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_int (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_int64 (kvsdir_t *dir, const char *name, int64_t *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_int64 (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_double (kvsdir_t *dir, const char *name, double *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_double (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_boolean (kvsdir_t *dir, const char *name, bool *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_boolean (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_put_obj (kvsdir_t *dir, const char *name, json_object *val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put (dir->handle, name, Jtostr (val));
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put (kvsdir_t *dir, const char *name, const char *val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_string (kvsdir_t *dir, const char *name, const char *val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_string (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_int (kvsdir_t *dir, const char *name, int val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_int (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_int64 (kvsdir_t *dir, const char *name, int64_t val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_int64 (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_double (kvsdir_t *dir, const char *name, double val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_double (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_boolean (kvsdir_t *dir, const char *name, bool val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_boolean (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_mkdir (kvsdir_t *dir, const char *name)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_mkdir (dir->handle, name);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_symlink (kvsdir_t *dir, const char *name, const char *target)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_symlink (dir->handle, name, target);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_unlink (kvsdir_t *dir, const char *name)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_unlink (dir->handle, name);
    kvs_popd (dir->handle);

    return (rc);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
