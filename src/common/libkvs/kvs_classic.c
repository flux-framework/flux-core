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
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

int flux_kvs_get (flux_t *h, const char *key, char **valp)
{
    flux_future_t *f;
    const char *json_str;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto done;
    if (valp) {
        if (!(*valp = strdup (json_str))) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int flux_kvs_get_dir (flux_t *h, flux_kvsdir_t **dirp, const char *fmt, ...)
{
    flux_future_t *f = NULL;
    const flux_kvsdir_t *dir;
    flux_kvsdir_t *cpy;
    va_list ap;
    char *key = NULL;
    int rc = -1;

    if (!h || !dirp || !fmt) {
        errno = EINVAL;
        goto done;
    }
    va_start (ap, fmt);
    if (vasprintf (&key, fmt, ap) < 0)
        errno = ENOMEM;
    va_end (ap);
    if (!key)
        goto done;
    /* N.B. python kvs tests use empty string key for some reason.
     * Don't break them for now.
     */
    const char *k = strlen (key) > 0 ? key : ".";

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READDIR, k)))
        goto done;
    if (flux_kvs_lookup_get_dir (f, &dir) < 0)
        goto done;
    if (!(cpy = flux_kvsdir_copy (dir)))
        goto done;
    *dirp = cpy;
    rc = 0;
done:
    free (key);
    flux_future_destroy (f);
    return rc;
}

int flux_kvsdir_get (const flux_kvsdir_t *dir, const char *name, char **valp)
{
    flux_t *h = flux_kvsdir_handle (dir);
    const char *rootref = flux_kvsdir_rootref (dir);
    flux_future_t *f = NULL;
    const char *json_str;
    char *key;
    int rc = -1;

    if (!(key = flux_kvsdir_key_at (dir, name)))
        goto done;
    if (!(f = flux_kvs_lookupat (h, 0, key, rootref)))
        goto done;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto done;
    if (valp) {
        if (!(*valp = strdup (json_str))) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    free (key);
    flux_future_destroy (f);
    return rc;
}

int flux_kvsdir_get_dir (const flux_kvsdir_t *dir, flux_kvsdir_t **dirp,
                         const char *fmt, ...)
{
    flux_t *h = flux_kvsdir_handle (dir);
    const char *rootref = flux_kvsdir_rootref (dir);
    flux_future_t *f = NULL;
    va_list ap;
    const flux_kvsdir_t *subdir;
    flux_kvsdir_t *cpy;
    char *name = NULL;
    char *key = NULL;
    int rc = -1;

    va_start (ap, fmt);
    if (vasprintf (&name, fmt, ap) < 0)
        errno = ENOMEM;
    va_end (ap);
    if (!name)
        goto done;
    if (!(key = flux_kvsdir_key_at (dir, name)))
        goto done;
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, rootref)))
        goto done;
    if (flux_kvs_lookup_get_dir (f, &subdir) < 0)
        goto done;
    if (!(cpy = flux_kvsdir_copy (subdir)))
        goto done;
    *dirp = cpy;
    rc = 0;
done:
    free (key);
    free (name);
    flux_future_destroy (f);
    return rc;
}

const char *default_txn_auxkey = "flux::kvs_default_txn";
static flux_kvs_txn_t *get_default_txn (flux_t *h)
{
    flux_kvs_txn_t *txn;

    if (!h) {
        errno = EINVAL;
        return NULL;
    }
    if (!(txn = flux_aux_get (h, default_txn_auxkey))) {
        if (!(txn = flux_kvs_txn_create ()))
            return NULL;
        if (flux_aux_set (h, default_txn_auxkey,
                          txn, (flux_free_f)flux_kvs_txn_destroy) < 0) {
            flux_kvs_txn_destroy (txn);
            return NULL;
        }
    }
    return txn;
}

static void clear_default_txn (flux_t *h)
{
    (void)flux_aux_set (h, default_txn_auxkey, NULL, NULL);
}

int flux_kvs_commit_anon (flux_t *h, int flags)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    flux_future_t *f;
    int rc, saved_errno;

    if (!txn)
        return -1;
    if (!(f = flux_kvs_commit (h, flags, txn))) /* no-op if NULL txn */
        return -1;
    rc = flux_future_get (f, NULL);
    saved_errno = errno;
    flux_future_destroy (f);
    clear_default_txn (h);
    errno = saved_errno;
    return rc;
}

int flux_kvs_fence_anon (flux_t *h, const char *name, int nprocs, int flags)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    flux_future_t *f;
    int rc, saved_errno;

    if (!txn)
        return -1;
    if (!(f = flux_kvs_fence (h, flags, name, nprocs, txn)))
        return -1;
    rc = flux_future_get (f, NULL);
    saved_errno = errno;
    flux_future_destroy (f);
    clear_default_txn (h);
    errno = saved_errno;
    return rc;
}

static int validate_json (const char *json_str)
{
    json_t *test;
    if (!(test = json_loads (json_str, JSON_DECODE_ANY, NULL))) {
        errno = EINVAL;
        return -1;
    }
    json_decref (test);
    return 0;
}

int flux_kvs_put (flux_t *h, const char *key, const char *json_str)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    int rc;
    if (!txn)
        return -1;
    if (json_str) {
        if (validate_json (json_str) < 0)
            return -1;
        rc = flux_kvs_txn_put (txn, 0, key, json_str);
    }
    else
        rc = flux_kvs_txn_unlink (txn, 0, key);
    return rc;
}

int flux_kvs_unlink (flux_t *h, const char *key)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_unlink (txn, 0, key);
}

int flux_kvs_symlink (flux_t *h, const char *key, const char *target)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_symlink (txn, 0, key, NULL, target);
}

int flux_kvs_mkdir (flux_t *h, const char *key)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_mkdir (txn, 0, key);
}

struct dir_put {
    char *key;
    flux_kvs_txn_t *txn;
    flux_t *h;
};

static void dir_put_fini (struct dir_put *sp)
{
    int saved_errno = errno;
    free (sp->key);
    errno = saved_errno;
}

static int dir_put_init (const flux_kvsdir_t *dir, const char *key,
                         struct dir_put *dp)
{
    memset (dp, 0, sizeof (struct dir_put));
    if (!dir || !key) {
        errno = EINVAL;
        return -1;
    }
    if (flux_kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    dp->h = flux_kvsdir_handle (dir);
    if (!(dp->txn = get_default_txn (dp->h)))
        return -1;
    if (!(dp->key = flux_kvsdir_key_at (dir, key)))
        return -1;
    return 0;
}

int flux_kvsdir_put (const flux_kvsdir_t *dir, const char *key,
                     const char *json_str)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    if (json_str) {
        if (validate_json (json_str) < 0)
            return -1;
        rc = flux_kvs_txn_put (dp.txn, 0, dp.key, json_str);
    }
    else
        rc = flux_kvs_txn_unlink (dp.txn, 0, dp.key);
    dir_put_fini (&dp);
    return rc;
}

int flux_kvsdir_pack (const flux_kvsdir_t *dir, const char *key,
                      const char *fmt, ...)
{
    struct dir_put dp;
    int rc;
    va_list ap;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    va_start (ap, fmt);
    rc = flux_kvs_txn_vpack (dp.txn, 0, dp.key, fmt, ap);
    va_end (ap);
    dir_put_fini (&dp);
    return rc;
}

int flux_kvsdir_unlink (const flux_kvsdir_t *dir, const char *key)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_unlink (dp.txn, 0, dp.key);
    dir_put_fini (&dp);
    return rc;
}

int flux_kvsdir_mkdir (const flux_kvsdir_t *dir, const char *key)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_mkdir (dp.txn, 0, dp.key);
    dir_put_fini (&dp);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
