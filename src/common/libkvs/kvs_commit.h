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

/* FLUX_KVS_TXN_COMPACT will currently consolidate appends to the same
 * key.  For example, an append of "A" to the key "foo" and the append
 * "B" to the key "foo" maybe consolidated into a single append of
 * "AB".
 *
 * Compacting transactions means that certain ordered lists of
 * operations will be illegal to compact and result in an error.  Most
 * notably, if a key has data appended to it, then is overwritten in
 * the same transaction, a compaction of appends is not possible.
 *
 * FLUX_KVS_SYNC will ensure all data flushed to the backing store and
 * the root reference is checkpointed.  It effectively performs a:
 *
 * content.flush on rank 0
 * checkpoint on the new root reference from the commit
 *
 * FLUX_KVS_SYNC only works against the primary KVS namespace.  If any
 * part of the content.flush or checkpoint fails an error will be
 * returned and the entire commit will fail.  For example, if a
 * content backing store is not loaded, ENOSYS will returned from this
 * commit.
 */
enum kvs_commit_flags {
    FLUX_KVS_NO_MERGE = 1, /* disallow commits to be mergeable with others */
    FLUX_KVS_TXN_COMPACT = 2, /* try to combine ops on same key within txn */
    FLUX_KVS_SYNC = 4, /* flush and checkpoint after commit is done */
};

flux_future_t *flux_kvs_commit (flux_t *h,
                                const char *ns,
                                int flags,
                                flux_kvs_txn_t *txn);

/* accessors can be used for commit futures */
int flux_kvs_commit_get_treeobj (flux_future_t *f, const char **treeobj);
int flux_kvs_commit_get_rootref (flux_future_t *f, const char **rootref);
int flux_kvs_commit_get_sequence (flux_future_t *f, int *rootseq);

#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_KVS_COMMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
