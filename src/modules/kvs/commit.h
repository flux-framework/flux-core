#ifndef _FLUX_KVS_COMMIT_H
#define _FLUX_KVS_COMMIT_H

#include <czmq.h>

#include "cache.h"
#include "fence.h"
#include "types.h"

typedef struct commit_mgr commit_mgr_t;
typedef struct commit commit_t;

typedef enum {
    COMMIT_PROCESS_ERROR = 1,
    COMMIT_PROCESS_LOAD_MISSING_REFS = 2,
    COMMIT_PROCESS_DIRTY_CACHE_ENTRIES = 3,
    COMMIT_PROCESS_FINISHED = 4,
} commit_process_t;

/*
 * commit_t API
 */

typedef int (*commit_ref_cb)(commit_t *c, const char *ref, void *data);

typedef int (*commit_cache_entry_cb)(commit_t *c,
                                     struct cache_entry *hp,
                                     void *data);

int commit_get_errnum (commit_t *c);

fence_t *commit_get_fence (commit_t *c);

/* returns aux data passed into commit_mgr_create() */
void *commit_get_aux (commit_t *c);

/* returns non-NULL only if process state complete (commit_process()
 * returns COMMIT_PROCESS_FINISHED) */
const char *commit_get_newroot_ref (commit_t *c);

/* Primary commit processing funtion.
 *
 * Pass in a commit_t that was obtained via
 * commit_mgr_get_ready_commit().
 *
 * Returns COMMIT_PROCESS_ERROR on error,
 * COMMIT_PROCESS_LOAD_MISSING_REFS stall & load,
 * COMMIT_PROCESS_DIRTY_CACHE_ENTRIES stall & process dirty cache
 * entries,
 * COMMIT_PROCESS_FINISHED all done
 *
 * on error, call commit_get_errnum() to get error number
 *
 * on stall & load, call commit_iter_missing_refs()
 *
 * on stall & process dirty cache entries, call
 * commit_iter_dirty_cache_entries() to process entries.
 *
 * on completion, call commit_get_newroot_ref() to get reference to
 * new root to be stored.
 */
commit_process_t commit_process (commit_t *c,
                                 int current_epoch,
                                 const href_t rootdir_ref);

/* on commit stall, iterate through all missing refs that the caller
 * should load into the cache
 *
 * return -1 in callback to break iteration
 */
int commit_iter_missing_refs (commit_t *c, commit_ref_cb cb, void *data);

/* on commit stall, iterate through all dirty cache entries that need
 * to be pushed to the content store or wait to be finished being sent
 * to content store.
 *
 * cache_entry_get_content_store_flag() can be used to indicate if it
 * should be sent to the content store or not (be sure to clear the
 * flag appropriately.)
 *
 * return -1 in callback to break iteration
 */
int commit_iter_dirty_cache_entries (commit_t *c,
                                     commit_cache_entry_cb cb,
                                     void *data);

/*
 * commit_mgr_t API
 */

commit_mgr_t *commit_mgr_create (struct cache *cache,
                                 const char *hash_name,
                                 void *aux);

void commit_mgr_destroy (commit_mgr_t *cm);

/* Add fence into the commit manager */
int commit_mgr_add_fence (commit_mgr_t *cm, fence_t *f);

/* Lookup a fence previously stored via commit_mgr_add_fence(), via name */
fence_t *commit_mgr_lookup_fence (commit_mgr_t *cm, const char *name);

/* commit_mgr_process_fence_request() should be called once per fence
 * request, after fence_add_request_data() has been called.
 *
 * If conditions are correct, will internally create at commit_t and
 * store it to a queue of ready to process commits.
 */
int commit_mgr_process_fence_request (commit_mgr_t *cm, fence_t *f);

/* returns true if there are commits ready for processing and are not
 * blocked, false if not.
 */
bool commit_mgr_commits_ready (commit_mgr_t *cm);

/* if commit_mgr_commits_ready() is true, return a ready commit to
 * process
 */
commit_t *commit_mgr_get_ready_commit (commit_mgr_t *cm);

/* remove a commit from the commit manager after it is done processing
 */
void commit_mgr_remove_commit (commit_mgr_t *cm, commit_t *c);

/* remove a fence from the commit manager */
void commit_mgr_remove_fence (commit_mgr_t *cm, const char *name);

int commit_mgr_get_noop_stores (commit_mgr_t *cm);
void commit_mgr_clear_noop_stores (commit_mgr_t *cm);

/* In internally stored ready commits (moved to ready status via
 * commit_mgr_process_fence_request()), merge them if they are capable
 * of being merged.
 */
void commit_mgr_merge_ready_commits (commit_mgr_t *cm);

#endif /* !_FLUX_KVS_COMMIT_H */
