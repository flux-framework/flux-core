/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KVS_COMMIT_H
#define _FLUX_CORE_KVS_COMMIT_H

#ifdef __cplusplus
extern "C" {
#endif

enum kvs_commit_flags {
    FLUX_KVS_NO_MERGE = 1, /* disallow commits to be mergeable with others */
};

flux_future_t *flux_kvs_commit (flux_t *h, const char *ns, int flags,
                                flux_kvs_txn_t *txn);

flux_future_t *flux_kvs_fence (flux_t *h, const char *ns, int flags,
                               const char *name, int nprocs,
                               flux_kvs_txn_t *txn);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_COMMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
