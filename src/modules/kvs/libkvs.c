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

#include "src/common/libutil/log.h"
#include "src/common/libutil/jsonutil.h"
#include "src/common/libutil/xzmalloc.h"

struct kvsdir_struct {
    flux_t handle;
    char *key;
    json_object *o;
};

struct kvsdir_iterator_struct {
    kvsdir_t dir;
    struct json_object_iterator next;
    struct json_object_iterator end;
};

typedef enum {
    WATCH_STRING, WATCH_INT, WATCH_INT64, WATCH_DOUBLE,
    WATCH_BOOLEAN, WATCH_OBJECT, WATCH_DIR,
} watch_type_t;

typedef struct {
    watch_type_t type;
    KVSSetF *set;
    void *arg;
} kvs_watcher_t;

typedef struct {
    zhash_t *watchers;
    char *cwd;
    zlist_t *dirstack;
} kvsctx_t;

static int watch_rep_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg);

static void freectx (kvsctx_t *ctx)
{
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
        flux_aux_set (h, "kvscli", ctx, (FluxFreeFn)freectx);
        (void)flux_msghandler_add (h, FLUX_MSGTYPE_RESPONSE, "kvs.watch",
                                                watch_rep_cb, ctx);
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
        if (asprintf (&path, "%s.%s", cwd, relpath) < 0)
            oom ();
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

void kvsdir_destroy (kvsdir_t dir)
{
    free (dir->key);
    json_object_put (dir->o);
    free (dir);
}

static kvsdir_t kvsdir_alloc (flux_t handle, const char *key, json_object *o)
{
    kvsdir_t dir = xzmalloc (sizeof (*dir));

    dir->handle = handle;
    dir->key = xstrdup (key);
    dir->o = o;
    json_object_get (dir->o);

    return dir;
}

const char *kvsdir_key (kvsdir_t dir)
{
    return dir->key;
}

void *kvsdir_handle (kvsdir_t dir)
{
    return dir->handle;
}

void kvsitr_rewind (kvsitr_t itr)
{
    itr->next = json_object_iter_begin (itr->dir->o); 
}

kvsitr_t kvsitr_create (kvsdir_t dir)
{
    kvsitr_t itr = xzmalloc (sizeof (*itr));

    itr->dir = dir;
    itr->next = json_object_iter_begin (itr->dir->o); 
    itr->end = json_object_iter_end (itr->dir->o); 

    return itr;
}

void kvsitr_destroy (kvsitr_t itr)
{
    free (itr); 
}

const char *kvsitr_next (kvsitr_t itr)
{
    const char *name = NULL;

    if (!json_object_iter_equal (&itr->end, &itr->next)) {
        name = json_object_iter_peek_name (&itr->next);
        (void)json_object_iter_next (&itr->next);
    }

    return name;
}

bool kvsdir_exists (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);

    return (dirent != NULL);
}

bool kvsdir_isdir (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);

    return (dirent && (json_object_object_get (dirent, "DIRREF") != NULL
                    || json_object_object_get (dirent, "DIRVAL") != NULL));
}

bool kvsdir_issymlink (kvsdir_t dir, const char *name)
{
    json_object *dirent = json_object_object_get (dir->o, name);

    return (dirent && json_object_object_get (dirent, "LINKVAL") != NULL);
}


char *kvsdir_key_at (kvsdir_t dir, const char *name)
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

int kvs_get (flux_t h, const char *key, json_object **valp)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    char *path = pathcat (kvs_getcwd (h), key);
    json_object *reply = NULL;
    int ret = -1;

    json_object_object_add (request, path, NULL);
    reply = flux_rpc (h, request, "kvs.get");
    if (!reply)
        goto done;
    if (!(val = json_object_object_get (reply, path))) {
        errno = ENOENT;
        goto done;
    }
    if (valp) {
        json_object_get (val);
        *valp = val;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (path)
        free (path);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_get_dir (flux_t h, kvsdir_t *dirp, const char *fmt, ...)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
    char *key, *path;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        oom ();
    va_end (ap);
    path = pathcat (kvs_getcwd (h), key);

    util_json_object_add_boolean (request, ".flag_directory", true);
    json_object_object_add (request, path, NULL);
    reply = flux_rpc (h, request, "kvs.get");
    if (!reply)
        goto done;
    if (!(val = json_object_object_get (reply, path))) {
        errno = ENOENT;
        goto done;
    }
    if (dirp)
        *dirp = kvsdir_alloc (h, path, val);
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    if (path)
        free (path);
    if (key)
        free (key);
    return ret;
}

int kvs_get_symlink (flux_t h, const char *key, char **valp)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    char *path = pathcat (kvs_getcwd (h), key);
    json_object *reply = NULL;
    const char *s;
    int ret = -1;

    util_json_object_add_boolean (request, ".flag_readlink", true);
    json_object_object_add (request, path, NULL);
    reply = flux_rpc (h, request, "kvs.get");
    if (!reply)
        goto done;
    if (!(val = json_object_object_get (reply, path))) {
        errno = ENOENT;
        goto done;
    }
    if (json_object_get_type (val) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    if (!(s = json_object_get_string (val))) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = xstrdup (s);
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (path)
        free (path);
    if (reply)
        json_object_put (reply);
    return ret;
}

int kvs_get_string (flux_t h, const char *key, char **valp)
{
    json_object *o = NULL;
    const char *s;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_string) {
        errno = EINVAL;
        goto done;
    }
    s = json_object_get_string (o);
    if (valp)
        *valp = xstrdup (s);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_int (flux_t h, const char *key, int *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_int64 (flux_t h, const char *key, int64_t *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_int) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_int64 (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_double (flux_t h, const char *key, double *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_double) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_double (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

int kvs_get_boolean (flux_t h, const char *key, bool *valp)
{
    json_object *o = NULL;
    int rc = -1;

    if (kvs_get (h, key, &o) < 0)
        goto done;
    if (json_object_get_type (o) != json_type_boolean) {
        errno = EINVAL;
        goto done;
    }
    if (valp)
        *valp = json_object_get_boolean (o);
    rc = 0;
done: 
    if (o)
        json_object_put (o);
    return rc;
}

/**
 ** WATCH
 **/

static int dispatch_watch (flux_t h, kvs_watcher_t *wp, const char *key,
                            json_object *val)
{
    int errnum = val ? 0 : ENOENT;
    int rc = -1;

    switch (wp->type) {
        case WATCH_STRING: {
            KVSSetStringF *set = (KVSSetStringF *)wp->set; 
            const char *s = val ? json_object_get_string (val) : NULL;
            rc = set (key, s, wp->arg, errnum);
            break;
        }
        case WATCH_INT: {
            KVSSetIntF *set = (KVSSetIntF *)wp->set; 
            int i = val ? json_object_get_int (val) : 0;
            rc = set (key, i, wp->arg, errnum);
            break;
        }
        case WATCH_INT64: {
            KVSSetInt64F *set = (KVSSetInt64F *)wp->set; 
            int64_t i = val ? json_object_get_int64 (val) : 0;
            rc = set (key, i, wp->arg, errnum);
            break;
        }
        case WATCH_DOUBLE: {
            KVSSetDoubleF *set = (KVSSetDoubleF *)wp->set; 
            double d = val ? json_object_get_double (val) : 0;
            rc = set (key, d, wp->arg, errnum);
            break;
        }
        case WATCH_BOOLEAN: {
            KVSSetBooleanF *set = (KVSSetBooleanF *)wp->set; 
            bool b = val ? json_object_get_boolean (val) : false;
            rc = set (key, b, wp->arg, errnum);
            break;
        }
        case WATCH_DIR: {
            KVSSetDirF *set = (KVSSetDirF *)wp->set;
            kvsdir_t dir = val ? kvsdir_alloc (h, key, val) : NULL;
            rc = set (key, dir, wp->arg, errnum);
            if (dir)
                kvsdir_destroy (dir);
            break;
        }
        case WATCH_OBJECT: {
            rc = wp->set (key, val, wp->arg, errnum);
            break;
        }
    }
    return rc;
}

static int watch_rep_cb (flux_t h, int typemask, zmsg_t **zmsg, void *arg)
{
    kvsctx_t *ctx = arg;
    json_object *reply = NULL;
    json_object_iter iter;
    kvs_watcher_t *wp;
    bool match = false;
    int rc = 0;

    if (flux_msg_decode (*zmsg, NULL, &reply) == 0 && reply != NULL) {
        json_object_object_foreachC (reply, iter) {
            if ((wp = zhash_lookup (ctx->watchers, iter.key))) {
                rc = dispatch_watch (h, wp, iter.key, iter.val);
                match = true;
                if (rc < 0)
                    break;
            }
        }
    }
    if (reply)
        json_object_put (reply);
    if (match)
        zmsg_destroy (zmsg);
    return rc;
}

static kvs_watcher_t *add_watcher (flux_t h, const char *key, watch_type_t type,
                                   KVSSetF *fun, void *arg)
{
    kvsctx_t *ctx = getctx (h);
    kvs_watcher_t *wp = xzmalloc (sizeof (*wp));

    wp->set = fun;
    wp->type = type;
    wp->arg = arg;

    /* If key is already being watched, the new watcher replaces the old.
     */
    zhash_update (ctx->watchers, key, wp);
    zhash_freefn (ctx->watchers, key, free);

    return wp;
}

/* The "callback" idiom vs the "once" idiom handle a NULL value differently,
 * so don't convert to ENOENT here.  NULL is a valid value for *valp.
 */
static int send_kvs_watch (flux_t h, const char *key, json_object **valp,
                           bool once, bool directory)
{
    json_object *val = NULL;
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;

    if (once) {
        json_object_object_add (request, key, *valp);
        util_json_object_add_boolean (request, ".flag_once", true);
    } else {
        json_object_object_add (request, key, NULL);
        util_json_object_add_boolean (request, ".flag_first", true);
    }
    if (directory)
        util_json_object_add_boolean (request, ".flag_directory", true);
    reply = flux_rpc (h, request, "kvs.watch");
    if (!reply)
        goto done;
    if ((val = json_object_object_get (reply, key)))
        json_object_get (val);
    *valp = val;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply);
    return ret;
}

/* *valp is IN/OUT parameter.
 * IN *valp is freed internally.  Caller must free OUT *val.
 */
int kvs_watch_once (flux_t h, const char *key, json_object **valp)
{
    int rc = -1;

    if (send_kvs_watch (h, key, valp, true, false) < 0)
        goto done;
    if (*valp == NULL) {
        errno = ENOENT;
        goto done;
    }
    rc = 0;
done:
    return rc;
}

int kvs_watch_once_int (flux_t h, const char *key, int *valp)
{
    json_object *val;
    int rc = -1;

    if (!(val = json_object_new_int (*valp)))
        oom ();
    if (send_kvs_watch (h, key, &val, true, false) < 0)
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

int kvs_watch_once_dir (flux_t h, kvsdir_t *dirp, const char *fmt, ...)
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
    if (send_kvs_watch (h, key, &val, true, true) < 0)
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

int kvs_watch (flux_t h, const char *key, KVSSetF *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_OBJECT, set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_dir (flux_t h, KVSSetDirF *set, void *arg, const char *fmt, ...)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    char *key;
    int rc = -1;
    va_list ap;

    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        oom ();
    va_end (ap);

    if (send_kvs_watch (h, key, &val, false, true) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_DIR, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    if (key)
        free (key);
    return rc;
}

int kvs_watch_string (flux_t h, const char *key, KVSSetStringF *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_STRING, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_int (flux_t h, const char *key, KVSSetIntF *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_int64 (flux_t h, const char *key, KVSSetInt64F *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_INT64, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_double (flux_t h, const char *key, KVSSetDoubleF *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_DOUBLE, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

int kvs_watch_boolean (flux_t h, const char *key, KVSSetBooleanF *set, void *arg)
{
    kvs_watcher_t *wp;
    json_object *val = NULL;
    int rc = -1;

    if (send_kvs_watch (h, key, &val, false, false) < 0)
        goto done;
    wp = add_watcher (h, key, WATCH_BOOLEAN, (KVSSetF *)set, arg);
    dispatch_watch (h, wp, key, val);
    rc = 0;
done:
    if (val)
        json_object_put (val);
    return rc;
}

/**
 ** PUT
 **/

int kvs_put (flux_t h, const char *key, json_object *val)
{
    json_object *request = util_json_object_new_object ();
    char *path = pathcat (kvs_getcwd (h), key);
    json_object *reply = NULL;
    int ret = -1;

    if (val)
        json_object_get (val);
    json_object_object_add (request, path, val);
    reply = flux_rpc (h, request, "kvs.put");
    if (!reply && errno > 0)
        goto done;
    if (reply) {
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (path)
        free (path);
    if (reply)
        json_object_put (reply);
    return ret;
}


int kvs_put_string (flux_t h, const char *key, const char *val)
{
    json_object *o = NULL;
    int rc = -1;

    if (val && !(o = json_object_new_string (val)))
        oom ();
    if (kvs_put (h, key, o) < 0)
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
    if (kvs_put (h, key, o) < 0)
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
    if (kvs_put (h, key, o) < 0)
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
    if (kvs_put (h, key, o) < 0)
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
    if (kvs_put (h, key, o) < 0)
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
    json_object *request = util_json_object_new_object ();
    char *path = pathcat (kvs_getcwd (h), key);
    json_object *reply = NULL;
    int ret = -1;
  
    util_json_object_add_boolean (request, ".flag_symlink", true);
    util_json_object_add_string (request, path, target);
    reply = flux_rpc (h, request, "kvs.put");
    if (!reply && errno > 0)
        goto done;
    if (reply) {
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (path)
        free (path);
    if (reply)
        json_object_put (reply); 
    return ret;
}

int kvs_mkdir (flux_t h, const char *key)
{
    json_object *request = util_json_object_new_object ();
    char *path = pathcat (kvs_getcwd (h), key);
    json_object *reply;
    int ret = -1;

    util_json_object_add_boolean (request, ".flag_mkdir", true);
    json_object_object_add (request, path, NULL); 
    reply = flux_rpc (h, request, "kvs.put");
    if (!reply && errno > 0)
        goto done;
    if (reply) {
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (path)
        free (path);
    if (reply)
        json_object_put (reply); 
    return ret;
}

/**
 ** Commit/synchronization
 **/

int kvs_commit (flux_t h)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
  
    reply = flux_rpc (h, request, "kvs.commit");
    if (!reply)
        goto done;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

int kvs_fence (flux_t h, const char *name, int nprocs)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
  
    util_json_object_add_string (request, ".arg_fence", name);
    util_json_object_add_int (request, ".arg_nprocs", nprocs);
    reply = flux_rpc (h, request, "kvs.commit");
    if (!reply)
        goto done;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

int kvs_get_version (flux_t h, int *versionp)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
    int version;
 
    reply = flux_rpc (h, request, "kvs.getroot");
    if (!reply)
        goto done;
    if (util_json_object_get_int (reply, "rootseq", &version) < 0) {
        errno = EPROTO;
        goto done;
    }
    *versionp = version;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

int kvs_wait_version (flux_t h, int version)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
 
    util_json_object_add_int (request, "rootseq", version);
    reply = flux_rpc (h, request, "kvs.sync");
    if (!reply)
        goto done;
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}

int kvs_dropcache (flux_t h)
{
    json_object *request = util_json_object_new_object ();
    json_object *reply = NULL;
    int ret = -1;
 
    reply = flux_rpc (h, request, "kvs.dropcache");
    if (!reply && errno > 0)
        goto done;
    if (reply) { 
        errno = EPROTO;
        goto done;
    }
    ret = 0;
done:
    if (request)
        json_object_put (request);
    if (reply)
        json_object_put (reply); 
    return ret;
}


/**
 ** kvsdir_t convenience functions
 **/

int kvsdir_get (kvsdir_t dir, const char *name, json_object **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_dir (kvsdir_t dir, kvsdir_t *dirp, const char *fmt, ...)
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

int kvsdir_get_symlink (kvsdir_t dir, const char *name, char **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_symlink (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_string (kvsdir_t dir, const char *name, char **valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_string (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_int (kvsdir_t dir, const char *name, int *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_int (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_int64 (kvsdir_t dir, const char *name, int64_t *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_int64 (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_double (kvsdir_t dir, const char *name, double *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_double (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_get_boolean (kvsdir_t dir, const char *name, bool *valp)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_get_boolean (dir->handle, name, valp);
    kvs_popd (dir->handle);

    return rc;
}

int kvsdir_put (kvsdir_t dir, const char *name, json_object *val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_string (kvsdir_t dir, const char *name, const char *val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_string (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_int (kvsdir_t dir, const char *name, int val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_int (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_int64 (kvsdir_t dir, const char *name, int64_t val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_int64 (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_double (kvsdir_t dir, const char *name, double val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_double (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_put_boolean (kvsdir_t dir, const char *name, bool val)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_put_boolean (dir->handle, name, val);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_mkdir (kvsdir_t dir, const char *name)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_mkdir (dir->handle, name);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_symlink (kvsdir_t dir, const char *name, const char *target)
{
    int rc;

    kvs_pushd (dir->handle, dir->key);
    rc = kvs_symlink (dir->handle, name, target);
    kvs_popd (dir->handle);

    return (rc);
}

int kvsdir_unlink (kvsdir_t dir, const char *name)
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
