/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _KVS_COMMIT_PRIVATE_H
#define _KVS_COMMIT_PRIVATE_H

flux_future_t *flux_kvs_commit_ns (flux_t *h,
                                   const char *ns,
                                   int flags,
                                   flux_kvs_txn_t *txn);

#endif /* !_KVS_COMMIT_PRIVATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
