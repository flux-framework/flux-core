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

int kvs_get_dir (flux_t *h, kvsdir_t **dir, const char *fmt, ...)
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

int kvsdir_get (kvsdir_t *dir, const char *name, char **valp)
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

int kvsdir_get_dir (kvsdir_t *dir, kvsdir_t **dirp, const char *fmt, ...)
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

int kvsdir_get_int (kvsdir_t *dir, const char *name, int *valp)
{
    flux_t *h = kvsdir_handle (dir);
    const char *rootref = kvsdir_rootref (dir);
    flux_future_t *f = NULL;
    int i;
    char *key;
    int rc = -1;

    if (!(key = kvsdir_key_at (dir, name)))
        goto done;
    if (!(f = flux_kvs_lookupat (h, 0, key, rootref)))
        goto done;
    if (flux_kvs_lookup_getf (f, "i", &i) < 0)
        goto done;
    if (valp)
        *valp = i;
    rc = 0;
done:
    free (key);
    flux_future_destroy (f);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
