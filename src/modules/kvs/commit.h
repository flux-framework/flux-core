#ifndef _FLUX_KVS_COMMIT_H
#define _FLUX_KVS_COMMIT_H

#include <czmq.h>

#include "src/common/libutil/shortjson.h"

#include "cache.h"
#include "fence.h"
#include "types.h"

typedef struct {
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
    void *aux;
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
commit_t *commit_create (fence_t *f, void *aux);

void commit_destroy (commit_t *c);

/*
 * commit_mgr_t API
 */

commit_mgr_t *commit_mgr_create (void *aux);

void commit_mgr_destroy (commit_mgr_t *cm);

#endif /* !_FLUX_KVS_COMMIT_H */
