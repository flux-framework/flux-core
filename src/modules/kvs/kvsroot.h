#ifndef _FLUX_KVS_KVSROOT_H
#define _FLUX_KVS_KVSROOT_H

#include <stdbool.h>
#include <flux/core.h>

#include "cache.h"
#include "kvstxn.h"
#include "treq.h"
#include "waitqueue.h"
#include "src/common/libutil/blobref.h"

typedef struct kvsroot_mgr kvsroot_mgr_t;

struct kvsroot {
    char *namespace;
    uint32_t owner;
    int seq;
    blobref_t ref;
    kvstxn_mgr_t *ktm;
    treq_mgr_t *trm;
    waitqueue_t *watchlist;
    int watchlist_lastrun_epoch;
    int flags;
    bool remove;
};

/* return -1 on error, 0 on success, 1 on success & to stop iterating */
typedef int (*kvsroot_root_f)(struct kvsroot *root, void *arg);

/* flux_t optional, if NULL logging will go to stderr */
/* void *arg passed as arg value to kvstxn_mgr_create() internally */
kvsroot_mgr_t *kvsroot_mgr_create (flux_t *h, void *arg);

void kvsroot_mgr_destroy (kvsroot_mgr_t *km);

int kvsroot_mgr_root_count (kvsroot_mgr_t *km);

struct kvsroot *kvsroot_mgr_create_root (kvsroot_mgr_t *km,
                                         struct cache *cache,
                                         const char *hash_name,
                                         const char *namespace,
                                         uint32_t owner,
                                         int flags);

int kvsroot_mgr_remove_root (kvsroot_mgr_t *km, const char *namespace);

/* returns NULL if not found */
struct kvsroot *kvsroot_mgr_lookup_root (kvsroot_mgr_t *km,
                                         const char *namespace);

/* safe lookup, will return NULL if root in process of being removed,
 * i.e. remove flag set to true */
struct kvsroot *kvsroot_mgr_lookup_root_safe (kvsroot_mgr_t *km,
                                              const char *namespace);

int kvsroot_mgr_iter_roots (kvsroot_mgr_t *km, kvsroot_root_f cb, void *arg);

#endif /* !_FLUX_KVS_KVSROOT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
