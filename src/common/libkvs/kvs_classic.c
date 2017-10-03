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
#include <czmq.h>
#include <flux/core.h>

int kvs_get (flux_t *h, const char *key, char **valp)
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

int kvs_get_dir (flux_t *h, flux_kvsdir_t **dir, const char *fmt, ...)
{
    flux_future_t *f = NULL;
    const char *json_str;
    va_list ap;
    char *key = NULL;
    int rc = -1;

    if (!h || !dir || !fmt) {
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
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto done;
    if (!(*dir = kvsdir_create (h, NULL, k, json_str)))
        goto done;
    rc = 0;
done:
    free (key);
    flux_future_destroy (f);
    return rc;
}

int kvsdir_get (flux_kvsdir_t *dir, const char *name, char **valp)
{
    flux_t *h = kvsdir_handle (dir);
    const char *rootref = kvsdir_rootref (dir);
    flux_future_t *f = NULL;
    const char *json_str;
    char *key;
    int rc = -1;

    if (!(key = kvsdir_key_at (dir, name)))
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

int kvsdir_get_dir (flux_kvsdir_t *dir, flux_kvsdir_t **dirp,
                    const char *fmt, ...)
{
    flux_t *h = kvsdir_handle (dir);
    const char *rootref = kvsdir_rootref (dir);
    flux_future_t *f = NULL;
    va_list ap;
    const char *json_str;
    char *name = NULL;
    char *key = NULL;
    int rc = -1;

    va_start (ap, fmt);
    if (vasprintf (&name, fmt, ap) < 0)
        errno = ENOMEM;
    va_end (ap);
    if (!name)
        goto done;
    if (!(key = kvsdir_key_at (dir, name)))
        goto done;
    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READDIR, key, rootref)))
        goto done;
    if (flux_kvs_lookup_get (f, &json_str) < 0)
        goto done;
    if (!(*dirp = kvsdir_create (h, rootref, key, json_str)))
        goto done;
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
    flux_kvs_txn_t *txn = NULL;

    if (!h) {
        errno = EINVAL;
        goto done;
    }
    if (!(txn = flux_aux_get (h, default_txn_auxkey))) {
        if (!(txn = flux_kvs_txn_create ()))
            goto done;
        flux_aux_set (h, default_txn_auxkey,
                      txn, (flux_free_f)flux_kvs_txn_destroy);
    }
done:
    return txn;
}

static void clear_default_txn (flux_t *h)
{
    flux_aux_set (h, default_txn_auxkey, NULL, NULL);
}

int kvs_commit (flux_t *h, int flags)
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

int kvs_fence (flux_t *h, const char *name, int nprocs, int flags)
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

int kvs_put (flux_t *h, const char *key, const char *json_str)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_put (txn, 0, key, json_str);
}

int kvs_put_string (flux_t *h, const char *key, const char *val)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_pack (txn, 0, key, "s", val);
}

int kvs_put_int (flux_t *h, const char *key, int val)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_pack (txn, 0, key, "i", val);
}

int kvs_put_int64 (flux_t *h, const char *key, int64_t val)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_pack (txn, 0, key, "I", val);
}

int kvs_unlink (flux_t *h, const char *key)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_unlink (txn, 0, key);
}

int kvs_symlink (flux_t *h, const char *key, const char *target)
{
    flux_kvs_txn_t *txn = get_default_txn (h);
    if (!txn)
        return -1;
    return flux_kvs_txn_symlink (txn, 0, key, target);
}

int kvs_mkdir (flux_t *h, const char *key)
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

static int dir_put_init (flux_kvsdir_t *dir, const char *key, struct dir_put *dp)
{
    memset (dp, 0, sizeof (struct dir_put));
    if (!dir || !key) {
        errno = EINVAL;
        return -1;
    }
    if (kvsdir_rootref (dir) != NULL) {
        errno = EROFS;
        return -1;
    }
    dp->h = kvsdir_handle (dir);
    if (!(dp->txn = get_default_txn (dp->h)))
        return -1;
    if (!(dp->key = kvsdir_key_at (dir, key)))
        return -1;
    return 0;
}

int kvsdir_put (flux_kvsdir_t *dir, const char *key, const char *json_str)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_put (dp.txn, 0, dp.key, json_str);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_put_string (flux_kvsdir_t *dir, const char *key, const char *val)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_pack (dp.txn, 0, dp.key, "s", val);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_put_int (flux_kvsdir_t *dir, const char *key, int val)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_pack (dp.txn, 0, dp.key, "i", val);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_put_int64 (flux_kvsdir_t *dir, const char *key, int64_t val)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_pack (dp.txn, 0, dp.key, "I", val);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_put_double (flux_kvsdir_t *dir, const char *key, double val)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_pack (dp.txn, 0, dp.key, "f", val);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_put_boolean (flux_kvsdir_t *dir, const char *key, bool val)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_pack (dp.txn, 0, dp.key, "b", (int)val);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_unlink (flux_kvsdir_t *dir, const char *key)
{
    struct dir_put dp;
    int rc;
    if (dir_put_init (dir, key, &dp) < 0)
        return -1;
    rc = flux_kvs_txn_unlink (dp.txn, 0, dp.key);
    dir_put_fini (&dp);
    return rc;
}

int kvsdir_mkdir (flux_kvsdir_t *dir, const char *key)
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
