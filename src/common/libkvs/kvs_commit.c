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
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_txn_private.h"
#include "src/common/libutil/blobref.h"

flux_future_t *flux_kvs_fence (flux_t *h, int flags, const char *name,
                               int nprocs, flux_kvs_txn_t *txn)
{
    const char *namespace;
    json_t *ops;

    if (!name || nprocs <= 0 || !txn) {
        errno = EINVAL;
        return NULL;
    }

    if (!(namespace = flux_kvs_get_namespace (h)))
        return NULL;

    if (!(ops = txn_get_ops (txn))) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h, "kvs.fence", FLUX_NODEID_ANY, 0,
                          "{s:s s:i s:s s:i s:O}",
                          "name", name,
                          "nprocs", nprocs,
                          "namespace", namespace,
                          "flags", flags,
                          "ops", ops);
}

flux_future_t *flux_kvs_commit_ns (flux_t *h,
                                   const char *namespace,
                                   int flags,
                                   flux_kvs_txn_t *txn)
{
    json_t *ops;

    if (!txn || !namespace) {
        errno = EINVAL;
        return NULL;
    }

    if (!(ops = txn_get_ops (txn))) {
        errno = EINVAL;
        return NULL;
    }

    return flux_rpc_pack (h, "kvs.commit", FLUX_NODEID_ANY, 0,
                          "{s:s s:i s:O}",
                          "namespace", namespace,
                          "flags", flags,
                          "ops", ops);
}

flux_future_t *flux_kvs_commit (flux_t *h, int flags, flux_kvs_txn_t *txn)
{
    const char *namespace;

    if (!(namespace = flux_kvs_get_namespace (h)))
        return NULL;

    return flux_kvs_commit_ns (h, namespace, flags, txn);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
