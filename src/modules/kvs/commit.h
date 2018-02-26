#ifndef _FLUX_KVS_COMMIT_H
#define _FLUX_KVS_COMMIT_H

#include <flux/core.h>
#include <czmq.h>

#include "cache.h"
#include "fence.h"
#include "src/common/libutil/blobref.h"

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

typedef int (*commit_ref_f)(commit_t *c, const char *ref, void *data);

typedef int (*commit_cache_entry_f)(commit_t *c,
                                     struct cache_entry *entry,
                                     void *data);

typedef int (*commit_fence_f)(fence_t *f, void *data);

int commit_get_errnum (commit_t *c);

/* if user wishes to stall, but needs future knowledge to fail and
 * what error caused the failure.
 */
int commit_get_aux_errnum (commit_t *c);
int commit_set_aux_errnum (commit_t *c, int errnum);

json_t *commit_get_ops (commit_t *c);
json_t *commit_get_names (commit_t *c);
int commit_get_flags (commit_t *c);

/* returns namespace passed into commit_mgr_create() */
const char *commit_get_namespace (commit_t *c);

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
                                 const blobref_t rootdir_ref);

/* on commit stall, iterate through all missing refs that the caller
 * should load into the cache
 *
 * return -1 in callback to break iteration
 */
int commit_iter_missing_refs (commit_t *c, commit_ref_f cb, void *data);

/* on commit stall, iterate through all dirty cache entries that need
 * to be pushed to the content store.
 *
 * return -1 in callback to break iteration
 */
int commit_iter_dirty_cache_entries (commit_t *c,
                                     commit_cache_entry_f cb,
                                     void *data);

/* convenience function for cleaning up a dirty cache entry that was
 * returned to the user via commit_process().  Generally speaking, this
 * should only be used for error cleanup in the callback function used in
 * commit_iter_dirty_cache_entries().
 */
void commit_cleanup_dirty_cache_entry (commit_t *c, struct cache_entry *entry);

/*
 * commit_mgr_t API
 */

/* flux_t is optional, if NULL logging will go to stderr */
commit_mgr_t *commit_mgr_create (struct cache *cache,
                                 const char *namespace,
                                 const char *hash_name,
                                 flux_t *h,
                                 void *aux);

void commit_mgr_destroy (commit_mgr_t *cm);

/* Add fence into the commit manager */
int commit_mgr_add_fence (commit_mgr_t *cm, fence_t *f);

/* Lookup a fence previously stored via commit_mgr_add_fence(), via name */
fence_t *commit_mgr_lookup_fence (commit_mgr_t *cm, const char *name);

/* Iterate through all fences in that have never had its operations
 * converted to a ready commit_t
 * - this is typically called during a needed cleanup path
 */
int commit_mgr_iter_not_ready_fences (commit_mgr_t *cm, commit_fence_f cb,
                                      void *data);

/* commit_mgr_process_fence_request() should be called once per fence
 * request, after fence_add_request_data() has been called.
 *
 * If conditions are correct, will internally create at commit_t and
 * store it to a queue of ready to process commits.
 *
 * The fence_t will have its processed flag set to true if a commit_t
 * is created and queued.  See fence_get/set_processed().
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
int commit_mgr_remove_fence (commit_mgr_t *cm, const char *name);

int commit_mgr_get_noop_stores (commit_mgr_t *cm);
void commit_mgr_clear_noop_stores (commit_mgr_t *cm);

/* Get count of fences stored */
int commit_mgr_fences_count (commit_mgr_t *cm);

/* return count of ready commits */
int commit_mgr_ready_commit_count (commit_mgr_t *cm);

/* In internally stored ready commits (moved to ready status via
 * commit_mgr_process_fence_request()), merge them if they are capable
 * of being merged.
 * Returns -1 on error, 0 on success.  On error, it is possible that
 * the ready commit has been modified with different fence names
 * and operations.  The caller is responsible for sending errors to
 * all appropriately.
 */
int commit_mgr_merge_ready_commits (commit_mgr_t *cm);

#endif /* !_FLUX_KVS_COMMIT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
