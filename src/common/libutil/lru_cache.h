
/*
 *  lru_cache_t - simple LRU cache interface
 */

#ifndef HAVE_LRU_CACHE_H
#define HAVE_LRU_CACHE_H

#include <stdbool.h>

typedef struct lru_cache lru_cache_t;
typedef void (*lru_cache_free_f) (void *data);

/*  Create a lru cache which holds at maximum `maxsize` objects
 */
lru_cache_t *lru_cache_create (int maxsize);
void lru_cache_destroy (lru_cache_t *lru);

/*  Set a function `fn` to free items stored in lru cache when they
 *   purged or removed with `lru_cache_remove`.
 */
void lru_cache_set_free_f (lru_cache_t *lru, lru_cache_free_f fn);

/*  Return current number of items stored in lru cache.
 */
int lru_cache_size (lru_cache_t *lru);

/*  Put item `value` into cache, associated by key `key`.
 *  Returns 0 on success, -1 on failure, with EEXIST if item already cached.
 *
 *  If value already existed in LRU cache, it will be moved to the front
 *  of the LRU list.
 */
int lru_cache_put (lru_cache_t *lru, const char *key, void *item);

/*  Get item associated with key. Returns NULL if item not found.
 *  This will also move the item, if found, to the front of the LRU list.
 */
void *lru_cache_get (lru_cache_t *lru, const char *key);

/*  Check if item with `key` is cached, without updating reference
 *   of the item (i.e. do not promote it on the LRU list)
 */
bool lru_cache_check (lru_cache_t *lru, const char *key);

/*
 *  Force removal of item associated with `key` from the LRU cache.
 */
int lru_cache_remove (lru_cache_t *lru, const char *key);

#endif /* !HAVE_LRU_CACHE_H */
