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
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_lookup.h"

flux_future_t *flux_kvs_lookup (flux_t *h, int flags, const char *key)
{
    return flux_rpc_pack (h, "kvs.get", FLUX_NODEID_ANY, 0, "{s:s s:i}",
                                                            "key", key,
                                                            "flags", flags);
}

flux_future_t *flux_kvs_lookupat (flux_t *h, int flags, const char *key,
                                  const char *treeobj)
{
    flux_future_t *f;
    json_t *obj = NULL;

    if (!treeobj) {
        f = flux_kvs_lookup (h, flags, key);
    }
    else {
        if (!(obj = json_loads (treeobj, 0, NULL))) {
            errno = EINVAL;
            return NULL;
        }
        f = flux_rpc_pack (h, "kvs.get", FLUX_NODEID_ANY, 0, "{s:s s:i s:O}",
                                                             "key", key,
                                                             "flags", flags,
                                                             "rootdir", obj);
    }
    json_decref (obj);
    return f;
}

int flux_kvs_lookup_get (flux_future_t *f, const char **json_str)
{
    const char *auxkey = "flux::kvs_valstr";
    json_t *obj;
    char *s;

    if (!(s = flux_future_aux_get (f, auxkey))) {
        if (flux_rpc_getf (f, "{s:o}", "val", &obj) < 0)
            return -1;
        if (!(s = json_dumps (obj, JSON_COMPACT|JSON_ENCODE_ANY))) {
            errno = EINVAL;
            return -1;
        }
        if (flux_future_aux_set (f, auxkey, s, free) < 0) {
            free (s);
            errno = ENOMEM;
            return -1;
        }
    }
    if (json_str)
        *json_str = s;
    return 0;
}

int flux_kvs_lookup_get_unpack (flux_future_t *f, const char *fmt, ...)
{
    va_list ap;
    json_t *obj;
    int rc;

    if (flux_rpc_getf (f, "{s:o}", "val", &obj) < 0)
        return -1;
    va_start (ap, fmt);
    if ((rc = json_vunpack_ex (obj, NULL, 0, fmt, ap) < 0))
        errno = EPROTO;
    va_end (ap);

    return rc;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
