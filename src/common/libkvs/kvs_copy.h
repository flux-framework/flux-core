/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_CORE_KVS_COPY_H
#define _FLUX_CORE_KVS_COPY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create a copy of 'srckey' at 'dstkey'.
 * Due to the hash-tree design of the KVS, dstkey is by definition a
 * "deep copy" (or writable snapshot) of all content below srckey.
 * The copy operation has a low overhead since it only copies a single
 * directory entry.  'srckey' and 'dstkey' may be in different namespaces.
 * Returns future on success, NULL on failure with errno set.
 */
flux_future_t *flux_kvs_copy (flux_t *h, const char *srckey,
                                         const char *dstkey,
                                         int commit_flags);

/* Move 'srckey' to 'dstkey'.
 * This is a copy followed by an unlink on 'srckey'.
 * 'srckey' and 'dstkey' may be in different namespaces.
 * The copy and unlink are not atomic.
 * Returns future on success, NULL on failure with errno set.
 */
flux_future_t *flux_kvs_move (flux_t *h, const char *srckey,
                                         const char *dstkey,
                                         int commit_flags);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_COPY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
