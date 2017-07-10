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


typedef struct {
    json_object *ops;   /* JSON array of put, unlink, etc operations */
    zhash_t *fence_ops;
    json_object *fence_context;
} kvsctx_t;

static void freectx (void *arg)
{
    kvsctx_t *ctx = arg;
    if (ctx) {
        zhash_destroy (&ctx->fence_ops);
        Jput (ctx->ops);
        free (ctx);
    }
}

static kvsctx_t *getctx (flux_t *h)
{
    const char *auxkey = "flux::kvs_client";
    kvsctx_t *ctx = (kvsctx_t *)flux_aux_get (h, auxkey);

    if (!ctx) {
        ctx = xzmalloc (sizeof (*ctx));
        flux_aux_set (h, auxkey, ctx, freectx);
    }
    return ctx;
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

    if (!(f = flux_rpc_pack (h, "kvs.sync", FLUX_NODEID_ANY, 0, "{ s:i }",
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

int kvsdir_put (kvsdir_t *dir, const char *name, const char *val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_string (kvsdir_t *dir, const char *name, const char *val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_string (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_int (kvsdir_t *dir, const char *name, int val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_int (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_int64 (kvsdir_t *dir, const char *name, int64_t val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_int64 (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_double (kvsdir_t *dir, const char *name, double val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_double (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_put_boolean (kvsdir_t *dir, const char *name, bool val)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_put_boolean (h, key, val);
    free (key);

    return (rc);
}

int kvsdir_mkdir (kvsdir_t *dir, const char *name)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_mkdir (h, key);
    free (key);

    return (rc);
}

int kvsdir_symlink (kvsdir_t *dir, const char *name, const char *target)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_symlink (h, key, target);
    free (key);

    return (rc);
}

int kvsdir_unlink (kvsdir_t *dir, const char *name)
{
    flux_t *h = kvsdir_handle (dir);
    int rc;
    char *key;

    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    key = kvsdir_key_at (dir, name);
    rc = kvs_unlink (h, key);
    free (key);

    return (rc);
}

int kvs_copy (flux_t *h, const char *from, const char *to)
{
    flux_future_t *f;
    const char *json_str;
    json_object *dirent = NULL;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, from)))
        goto done;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto done;
    if (!(dirent = Jfromstr (json_str))) {
        errno = EPROTO;
        goto done;
    }
    if (kvs_put_dirent (h, to, dirent) < 0) // steals dirent reference
        goto done;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
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
