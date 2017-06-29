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

int kvs_get_string (flux_t *h, const char *key, char **valp)
{
    flux_future_t *f;
    const char *s;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "s", &s) < 0)
        goto done;
    if (valp) {
        if (!(*valp = strdup (s))) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_int (flux_t *h, const char *key, int *valp)
{
    flux_future_t *f;
    int i;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "i", &i) < 0)
        goto done;
    if (valp)
        *valp = i;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_int64 (flux_t *h, const char *key, int64_t *valp)
{
    flux_future_t *f;
    int64_t i;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "I",  &i) < 0)
        goto done;
    if (valp)
        *valp = i;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_double (flux_t *h, const char *key, double *valp)
{
    flux_future_t *f;
    double d;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "F", &d) < 0)
        goto done;
    if (valp)
        *valp = d;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_boolean (flux_t *h, const char *key, bool *valp)
{
    flux_future_t *f;
    int b;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, 0, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "b", &b) < 0)
        goto done;
    if (valp)
        *valp = b;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_symlink (flux_t *h, const char *key, char **valp)
{
    flux_future_t *f;
    const char *s;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_READLINK, key)))
        goto done;
    if (flux_kvs_lookup_getf (f, "s", &s) < 0)
        goto done;
    if (valp) {
        if (!(*valp = strdup (s))) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int kvs_get_treeobj (flux_t *h, const char *key, char **valp)
{
    flux_future_t *f;
    const char *json_str;
    int rc = -1;

    if (!(f = flux_kvs_lookup (h, FLUX_KVS_TREEOBJ, key)))
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

int kvs_getat (flux_t *h, const char *treeobj,
               const char *key, char **valp)
{
    flux_future_t *f;
    const char *json_str;
    int rc = -1;

    if (!(f = flux_kvs_lookupat (h, 0, key, treeobj)))
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

int kvs_get_symlinkat (flux_t *h, const char *treeobj,
                       const char *key, char **valp)
{
    flux_future_t *f;
    const char *s;
    int rc = -1;

    if (!(f = flux_kvs_lookupat (h, FLUX_KVS_READLINK, key, treeobj)))
        goto done;
    if (flux_kvs_lookup_getf (f, "s", &s) < 0)
        goto done;
    if (valp) {
        if (!(*valp = strdup (s))) {
            errno = ENOMEM;
            goto done;
        }
    }
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
