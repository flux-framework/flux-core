#ifndef _FLUX_KVS_COMMIT_H
#define _FLUX_KVS_COMMIT_H

#include <czmq.h>

#include "src/common/libutil/shortjson.h"

#include "cache.h"
#include "fence.h"
#include "types.h"

typedef struct {
    struct cache *cache;
    const char *hash_name;
    int noop_stores;            /* for kvs.stats.get, etc.*/
    zhash_t *fences;
    zlist_t *ready;
    void *aux;
} commit_mgr_t;

typedef struct {
    int errnum;
    fence_t *f;
    int blocked:1;
    json_object *rootcpy;   /* working copy of root dir */
    href_t newroot;
    zlist_t *missing_refs;
    zlist_t *dirty_cache_entries;
    commit_mgr_t *cm;
    enum {
        COMMIT_STATE_INIT = 1,
        COMMIT_STATE_LOAD_ROOT = 2,
        COMMIT_STATE_APPLY_OPS = 3,
        COMMIT_STATE_STORE = 4,
        COMMIT_STATE_PRE_FINISHED = 5,
        COMMIT_STATE_FINISHED = 6,
    } state;
} commit_t;

/*
 * commit_t API
 */

int commit_get_errnum (commit_t *c);

fence_t *commit_get_fence (commit_t *c);

/* returns aux data passed into commit_mgr_create() */
void *commit_get_aux (commit_t *c);

const char *commit_get_newroot_ref (commit_t *c);

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
