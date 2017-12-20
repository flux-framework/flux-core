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
#include <flux/core.h>

const char *get_kvs_namespace (void)
{
    if (getenv ("FLUX_KVS_NAMESPACE"))
        return getenv ("FLUX_KVS_NAMESPACE");
    return KVS_PRIMARY_NAMESPACE;
}

flux_future_t *flux_kvs_namespace_create (flux_t *h, const char *namespace,
                                          int flags)
{
    if (!namespace || flags) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h, "kvs.namespace.create", 0, 0,
                          "{ s:s s:i }",
                          "namespace", namespace,
                          "flags", flags);
}

flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *namespace)
{
    if (!namespace) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h, "kvs.namespace.remove", 0, 0,
                          "{ s:s }",
                          "namespace", namespace);
}

int flux_kvs_get_version (flux_t *h, int *versionp)
{
    flux_future_t *f;
    const char *namespace = get_kvs_namespace ();
    int version;
    int rc = -1;

    if (!(f = flux_rpc_pack (h, "kvs.getroot", FLUX_NODEID_ANY, 0, "{ s:s }",
                             "namespace", namespace)))
        goto done;
    if (flux_rpc_get_unpack (f, "{ s:i }", "rootseq", &version) < 0)
        goto done;
    if (versionp)
        *versionp = version;
    rc = 0;
done:
    flux_future_destroy (f);
    return rc;
}

int flux_kvs_wait_version (flux_t *h, int version)
{
    flux_future_t *f;
    const char *namespace = get_kvs_namespace ();
    int ret = -1;

    if (!(f = flux_rpc_pack (h, "kvs.sync", FLUX_NODEID_ANY, 0, "{ s:i s:s }",
                             "rootseq", version,
                             "namespace", namespace)))
        goto done;
    /* N.B. response contains (rootseq, rootref) but we don't need it.
     */
    if (flux_future_get (f, NULL) < 0)
        goto done;
    ret = 0;
done:
    flux_future_destroy (f);
    return ret;
}

int flux_kvs_dropcache (flux_t *h)
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
