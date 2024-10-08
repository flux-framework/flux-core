/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_KVSTXN_H
#define _FLUX_KVS_KVSTXN_H

#include <flux/core.h>

#include "cache.h"

typedef struct kvstxn_mgr kvstxn_mgr_t;
typedef struct kvstxn kvstxn_t;

typedef enum {
    KVSTXN_PROCESS_ERROR = 1,
    KVSTXN_PROCESS_LOAD_MISSING_REFS = 2,
    KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES = 3,
    KVSTXN_PROCESS_SYNC_CONTENT_FLUSH = 4,
    KVSTXN_PROCESS_SYNC_CHECKPOINT = 5,
    KVSTXN_PROCESS_FINISHED = 6,
} kvstxn_process_t;

/* api flags, to be used with kvstxn_mgr_add_transaction()
 *
 * KVSTXN_INTERNAL_FLAG_NO_PUBLISH - Indicate that this transaction
 * should not publish its change after the transaction completes.
 * Note that kvstxn does not use this flag internally, users can check
 * that it has been set via kvstxn_get_internal_flags().
 */

#define KVSTXN_INTERNAL_FLAG_NO_PUBLISH 0x01

/*
 * kvstxn_t API
 */

typedef int (*kvstxn_ref_f)(kvstxn_t *kt, const char *ref, void *data);

typedef int (*kvstxn_cache_entry_f)(kvstxn_t *kt,
                                    struct cache_entry *entry,
                                    void *data);

int kvstxn_get_errnum (kvstxn_t *kt);

/* if user wishes to stall, but needs future knowledge to fail and
 * what error caused the failure.
 */
int kvstxn_get_aux_errnum (kvstxn_t *kt);
int kvstxn_set_aux_errnum (kvstxn_t *kt, int errnum);

/* Returns true if a kvstxn was merged and the user can fallback to
 * the original transactions that it was made up of.  This function
 * should be used when a merged kvstxn has failed.  Instead of failing
 * all transactions in this merged kvstxn, the kvstxn manager can be
 * told to fallback to the original transactions via a flag in
 * kvstxn_mgr_remove_transaction().  By falling back to the original
 * transactions, each can be played one by one and only the specific
 * failing transaction can be sent an error.  See
 * kvstxn_mgr_remove_kvstxn() below for more details.
 */
bool kvstxn_fallback_mergeable (kvstxn_t *kt);

json_t *kvstxn_get_ops (kvstxn_t *kt);
json_t *kvstxn_get_names (kvstxn_t *kt);
int kvstxn_get_flags (kvstxn_t *kt);
int kvstxn_get_internal_flags (kvstxn_t *kt);

/* returns namespace passed into kvstxn_mgr_create() */
const char *kvstxn_get_namespace (kvstxn_t *kt);

/* returns aux data passed into kvstxn_mgr_create() */
void *kvstxn_get_aux (kvstxn_t *kt);

/* returns non-NULL only if process state complete
 * (i.e. kvstxn_process() returns KVSTXN_PROCESS_FINISHED) */
const char *kvstxn_get_newroot_ref (kvstxn_t *kt);

/* returns non-NULL only if process state complete
 * (i.e. kvstxn_process() returns KVSTXN_PROCESS_FINISHED) */
json_t *kvstxn_get_keys (kvstxn_t *kt);

/* Primary transaction processing function.
 *
 * Pass in a kvstxn_t that was obtained via
 * kvstxn_mgr_get_ready_transaction().
 *
 * Returns KVSTXN_PROCESS_ERROR on error,
 * KVSTXN_PROCESS_LOAD_MISSING_REFS stall & load,
 * KVSTXN_PROCESS_DIRTY_CACHE_ENTRIES stall & process dirty cache
 * entries,
 * KVSTXN_PROCESS_SYNC_CONTENT_FLUSH stall & wait for future to fulfill
 * KVSTXN_PROCESS_SYNC_CHECKPOINT stall & wait for future to fulfill
 * KVSTXN_PROCESS_FINISHED all done
 *
 * on error, call kvstxn_get_errnum() to get error number
 *
 * on stall & load, call kvstxn_iter_missing_refs()
 *
 * on stall & process dirty cache entries, call
 * kvstxn_iter_dirty_cache_entries() to process entries.
 *
 * on stall & content-flush, call kvstxn_sync_content_flush() to get future.
 *
 * on stall & checkpoint, call kvstxn_sync_checkpoint() to get future.
 *
 * on completion, call kvstxn_get_newroot_ref() to get reference to
 * new root to be stored.
 */
kvstxn_process_t kvstxn_process (kvstxn_t *kt,
                                 const char *root_ref,
                                 int root_seq);

/* on stall, iterate through all missing refs that the caller should
 * load into the cache
 *
 * return -1 in callback to break iteration
 */
int kvstxn_iter_missing_refs (kvstxn_t *kt, kvstxn_ref_f cb, void *data);

/* on stall, iterate through all dirty cache entries that need to be
 * pushed to the content store.
 *
 * return -1 in callback to break iteration
 */
int kvstxn_iter_dirty_cache_entries (kvstxn_t *kt,
                                     kvstxn_cache_entry_f cb,
                                     void *data);

/* convenience function for cleaning up a dirty cache entry that was
 * returned to the user via kvstxn_process().  Generally speaking, this
 * should only be used for error cleanup in the callback function used in
 * kvstxn_iter_dirty_cache_entries().
 */
void kvstxn_cleanup_dirty_cache_entry (kvstxn_t *kt, struct cache_entry *entry);

/* on stall, get confent.flush future to wait for fulfillment on */
flux_future_t *kvstxn_sync_content_flush (kvstxn_t *kt);

/* on stall, get checkpoint future to wait for fulfillment on */
flux_future_t *kvstxn_sync_checkpoint (kvstxn_t *kt);

/*
 * kvstxn_mgr_t API
 */

/* flux_t is optional, if NULL logging will go to stderr */
kvstxn_mgr_t *kvstxn_mgr_create (struct cache *cache,
                                 const char *ns,
                                 const char *hash_name,
                                 flux_t *h,
                                 void *aux);

void kvstxn_mgr_destroy (kvstxn_mgr_t *ktm);

/* kvstxn_mgr_add_transaction() will internally create a kvstxn_t and
 * store it in the queue of ready to process transactions.
 *
 * This should be called once per transaction (commit or fence)
 * request.
 */
int kvstxn_mgr_add_transaction (kvstxn_mgr_t *ktm,
                                const char *name,
                                json_t *ops,
                                int flags,
                                int internal_flags);

/* returns true if there is a transaction ready for processing and is
 * not blocked, false if not.
 */
bool kvstxn_mgr_transaction_ready (kvstxn_mgr_t *ktm);

/* if kvstxn_mgr_transactions_ready() is true, return a ready
 * transaction to process
 */
kvstxn_t *kvstxn_mgr_get_ready_transaction (kvstxn_mgr_t *ktm);

/* remove a transaction from the kvstxn manager after it is done
 * processing
 *
 * If the kvstxn was merged, and the caller would like to fallback to
 * the original individual transactions (so they can be retried
 * individually), set `fallback` to true.  This will put the original
 * transactions back on the ready queue, but will make it so they
 * cannot be merged in the future (e.g. setting FLUX_KVS_NO_MERGE on
 * them).
 *
 * Be careful with the 'fallback' option.  If a transaction was
 * successful, you can still fallback the merged kvstxn into its
 * individual components.  'fallback' should only be set when you get
 * an error (i.e. you don't use kvstxn_get_newroot_ref to get a new
 * root).
 */
void kvstxn_mgr_remove_transaction (kvstxn_mgr_t *ktm,
                                    kvstxn_t *kt,
                                    bool fallback);

int kvstxn_mgr_get_noop_stores (kvstxn_mgr_t *ktm);
void kvstxn_mgr_clear_noop_stores (kvstxn_mgr_t *ktm);

/* return count of ready transactions */
int kvstxn_mgr_ready_transaction_count (kvstxn_mgr_t *ktm);

/* In internally stored ready transactions (moved to ready status via
 * kvstxn_mgr_add_transaction()), merge them into a new ready transaction
 * if they are capable of being merged.
 *
 * Callers should be cautioned to re-call
 * kvstxn_mgr_get_ready_transaction() for the new head commit as the
 * prior one has been removed.
 *
 * A merged kvstxn can be backed out if an error occurs.  See
 * kvstxn_fallback_mergeable() and kvstxn_mgr_remove_transaction()
 * above.
 *
 * Returns -1 on error, 0 on success.
 */
int kvstxn_mgr_merge_ready_transactions (kvstxn_mgr_t *ktm);

#endif /* !_FLUX_KVS_KVSTXN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
