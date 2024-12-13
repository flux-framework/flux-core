/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KVS_H
#define _FLUX_CORE_KVS_H

#include <flux/core.h>

#include "kvs_dir.h"
#include "kvs_lookup.h"
#include "kvs_getroot.h"
#include "kvs_txn.h"
#include "kvs_commit.h"
#include "kvs_copy.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KVS_PRIMARY_NAMESPACE "primary"

enum kvs_op {
    FLUX_KVS_READDIR = 1,
    FLUX_KVS_READLINK = 2,
    FLUX_KVS_WATCH = 4,
    FLUX_KVS_WAITCREATE = 8,
    FLUX_KVS_TREEOBJ = 16,
    FLUX_KVS_APPEND = 32,
    FLUX_KVS_WATCH_FULL = 64,
    FLUX_KVS_WATCH_UNIQ = 128,
    FLUX_KVS_WATCH_APPEND = 256,
    FLUX_KVS_STREAM = 512
};

/* Namespace
 * - namespace create only creates the namespace on rank 0.  Other
 *   ranks initialize against that namespace the first time they use
 *   it.
 * - namespace remove marks the namespace for removal on all ranks.
 *   Garbage collection will happen in the background and the
 *   namespace will official be removed.  The removal is "eventually
 *   consistent".
 */
flux_future_t *flux_kvs_namespace_create (flux_t *h,
                                          const char *ns,
                                          uint32_t owner,
                                          int flags);
flux_future_t *flux_kvs_namespace_create_with (flux_t *h,
                                               const char *ns,
                                               const char *rootref,
                                               uint32_t owner,
                                               int flags);
flux_future_t *flux_kvs_namespace_remove (flux_t *h, const char *ns);

/* Synchronization:
 * Process A commits data, then gets the store version V and sends it to B.
 * Process B waits for the store version to be >= V, then reads data.
 */
int flux_kvs_get_version (flux_t *h, const char *ns, int *versionp);
int flux_kvs_wait_version (flux_t *h, const char *ns, int version);

/* Garbage collect the cache.  Drop all data that doesn't have a
 * reference in the namespace.
 * Returns -1 on error (errno set), 0 on success.
 */
int flux_kvs_dropcache (flux_t *h);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
