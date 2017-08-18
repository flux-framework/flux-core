#ifndef _FLUX_CORE_KVS_H
#define _FLUX_CORE_KVS_H

#include <stdbool.h>
#include <stdint.h>
#include <flux/core.h>

#include "kvs_lookup.h"
#include "kvs_dir.h"
#include "kvs_classic.h"
#include "kvs_watch.h"
#include "kvs_txn.h"
#include "kvs_commit.h"

/* Synchronization:
 * Process A commits data, then gets the store version V and sends it to B.
 * Process B waits for the store version to be >= V, then reads data.
 */
int kvs_get_version (flux_t *h, int *versionp);
int kvs_wait_version (flux_t *h, int version);

/* Garbage collect the cache.  On the root node, drop all data that
 * doesn't have a reference in the namespace.  On other nodes, the entire
 * cache is dropped and will be reloaded on demand.
 * Returns -1 on error (errno set), 0 on success.
 */
int kvs_dropcache (flux_t *h);

#endif /* !_FLUX_CORE_KVS_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
