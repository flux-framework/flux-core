#ifndef _FLUX_KVS_CACHE_H
#define _FLUX_KVS_CACHE_H

#include <jansson.h>

#include "src/common/libutil/tstat.h"
#include "waitqueue.h"

struct cache_entry;
struct cache;


/* Create/destroy cache entry.
 * If non-NULL, create transfers ownership of 'o' to the cache entry.
 */
struct cache_entry *cache_entry_create (json_t *o);
void cache_entry_destroy (void *arg);

/* Return true if cache entry contains valid json.
 * False would indicate that a load RPC is in progress.
 */
bool cache_entry_get_valid (struct cache_entry *hp);

/* Get/set cache entry's dirty bit.
 * The dirty bit indicates that a store RPC is in progress.
 * A true->false transitions runs the entry's wait queue, if any.
 */
bool cache_entry_get_dirty (struct cache_entry *hp);
void cache_entry_set_dirty (struct cache_entry *hp, bool val);

/* cache_entry_clear_dirty() is similar to calling
 * cache_entry_set_dirty(hp,false), but it will not set to the dirty
 * bit to false if there are waiters for notdirty
 * (i.e. cache_entry_wait_notdirty() has been called on this entry).
 * This is typically called in an error path where the caller wishes
 * to give up on a previously marked dirty cache entry but has not yet
 * done anything with it.  Returns current value of dirty bit on
 * success, -1 on error.
 */
int cache_entry_clear_dirty (struct cache_entry *hp);

/* Accessors for cache entry data.
 * If non-NULL, set transfers ownership of 'o' to the cache entry.
 * An invalid->valid transition runs the entry's wait queue, if any.
 */
json_t *cache_entry_get_json (struct cache_entry *hp);
void cache_entry_set_json (struct cache_entry *hp, json_t *o);
int cache_entry_clear_json (struct cache_entry *hp);

/* Arrange for message handler represented by 'wait' to be restarted
 * once cache entry becomes valid or not dirty at completion of a
 * load or store RPC.
 */
void cache_entry_wait_notdirty (struct cache_entry *hp, wait_t *wait);
void cache_entry_wait_valid (struct cache_entry *hp, wait_t *wait);

/* Create/destroy the cache container and its contents.
 */
struct cache *cache_create (void);
void cache_destroy (struct cache *cache);

/* Look up a cache entry.
 * Update the entry's "last used" time to 'current_epoch',
 * taking care not to not run backwards.
 */
struct cache_entry *cache_lookup (struct cache *cache,
                                  const char *ref, int current_epoch);

/* Look up a cache entry and get json of cache entry only if entry
 * contains valid json.  This is a convenience function that is
 * effectively successful if calls to cache_lookup() and
 * cache_entry_get_json() are both successful.
 */
json_t *cache_lookup_and_get_json (struct cache *cache,
                                   const char *ref,
                                   int current_epoch);

/* Insert an entry in the cache by blobref 'ref'.
 * Ownership of the cache entry is transferred to the cache.
 */
void cache_insert (struct cache *cache, const char *ref,
                   struct cache_entry *hp);

/* Return the number of cache entries.
 */
int cache_count_entries (struct cache *cache);

/* Expire cache entries that are not dirty, not incomplete, and last
 * used more than 'thresh' epoch's ago.
 */
int cache_expire_entries (struct cache *cache, int current_epoch, int thresh);

/* Obtain statistics on the cache.
 */
void cache_get_stats (struct cache *cache, tstat_t *ts, int *size,
                      int *incomplete, int *dirty);

/* Destroy wait_t's on the waitqueue_t of any cache entry
 * if they meet match criteria.
 */
int cache_wait_destroy_msg (struct cache *cache, wait_test_msg_f cb, void *arg);

#endif /* !_FLUX_KVS_CACHE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
