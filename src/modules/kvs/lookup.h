#include "cache.h"

typedef struct lookup lookup_t;

/* Initialize a lookup handle
 * - If root_ref is same as root_dir, can be set to NULL.
 */
lookup_t *lookup_create (struct cache *cache,
                         int current_epoch,
                         const char *root_dir,
                         const char *root_ref,
                         const char *path,
                         int flags);

/* Destroy a lookup handle */
void lookup_destroy (lookup_t *lh);

/* Determine if lookup handle valid */
bool lookup_validate (lookup_t *lh);

/* Get errnum, should be checked after lookup() returns true to see if
 * an error occurred or not */
int lookup_get_errnum (lookup_t *lh);

/* Get resulting value of lookup() after lookup() returns true.  The
 * json object returned gives a reference to the caller and must be
 * json_object_put()'ed to free memory. */
json_object *lookup_get_value (lookup_t *lh);

/* Get missing ref after a lookup stall, missing reference can then be
 * used to load reference into the KVS cache */
const char *lookup_get_missing_ref (lookup_t *lh);

/* Convenience function to get cache from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
struct cache *lookup_get_cache (lookup_t *lh);

/* Convenience function to get current epoch from earlier
 * instantiation.  Convenient if replaying RPC and don't have it
 * presently.
 */
int lookup_get_current_epoch (lookup_t *lh);

/* Convenience function to get root dir from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
const char *lookup_get_root_dir (lookup_t *lh);

/* Convenience function to get root ref from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
const char *lookup_get_root_ref (lookup_t *lh);

/* Convenience function to get path from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
const char *lookup_get_path (lookup_t *lh);

/* Convenience function to get flags from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
int lookup_get_flags (lookup_t *lh);

/* Get auxiliarry data set by user */
void *lookup_get_aux_data (lookup_t *lh);

/* Set a new current epoch.  Convenience on RPC replays and epoch may
 * be new */
int lookup_set_current_epoch (lookup_t *lh, int epoch);

/* Set auxiliarry data for convenience.  User is responsible for
 * freeing data.
 */
int lookup_set_aux_data (lookup_t *lh, void *data);

/* Lookup the key path in the KVS cache starting at root.
 *
 * Return true on success or error.  After return, error should be
 * checked via lookup_get_errnum().  On success, value of resulting
 * lookup can be retrieved via lookup_get_value().
 *
 * Return false if key name cannot be resolved.  Get missing reference
 * in via lookup_get_missing_ref().  Caller should then use
 * missing reference to load missing reference into KVS cache via rpc
 * or otherwise.
 */
bool lookup (lookup_t *lh);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
