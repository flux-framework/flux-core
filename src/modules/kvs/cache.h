struct cache_entry;
struct cache;


/* Create/destroy cache entry.
 * If non-NULL, create transfers ownership of 'o' to the cache entry.
 */
struct cache_entry *cache_entry_create (json_object *o);
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

/* Accessors for cache entry data.
 * If non-NULL, set transfers ownership of 'o' to the cache entry.
 * An invalid->valid transition runs the entry's wait queue, if any.
 */
json_object *cache_entry_get_json (struct cache_entry *hp);
void cache_entry_set_json (struct cache_entry *hp, json_object *o);

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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
