#ifndef _FLUX_KVS_KVSROOT_H
#define _FLUX_KVS_KVSROOT_H

#include <stdbool.h>
#include <flux/core.h>
#include <czmq.h>

#include "cache.h"
#include "commit.h"
#include "waitqueue.h"
#include "src/common/libutil/blobref.h"

struct kvsroot_mgr {
    zhash_t *roothash;
};

typedef struct kvsroot_mgr kvsroot_mgr_t;

struct kvsroot {
    char *namespace;
    int seq;
    blobref_t ref;
    commit_mgr_t *cm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int flags;
    bool remove;
};

/* return -1 on error, 0 on success, 1 on success & to stop iterating */
typedef int (*kvsroot_root_f)(struct kvsroot *root, void *arg);

kvsroot_mgr_t *kvsroot_mgr_create (void);

void kvsroot_mgr_destroy (kvsroot_mgr_t *km);

void kvsroot_remove (zhash_t *roothash, const char *namespace);

/* returns NULL if not found */
struct kvsroot *kvsroot_lookup (zhash_t *roothash, const char *namespace);

/* safe lookup, will return NULL if root in process of being removed,
 * i.e. remove flag set to true */
struct kvsroot *kvsroot_lookup_safe (zhash_t *roothash, const char *namespace);

struct kvsroot *kvsroot_create (zhash_t *roothash,
                                struct cache *cache,
                                const char *hash_name,
                                const char *namespace,
                                int flags,
                                flux_t *h,
                                void *arg);

int kvsroot_iter (zhash_t *roothash, kvsroot_root_f cb, void *arg);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
