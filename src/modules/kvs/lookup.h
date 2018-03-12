#ifndef _FLUX_KVS_LOOKUP_H
#define _FLUX_KVS_LOOKUP_H

#include <flux/core.h>
#include "cache.h"
#include "kvsroot.h"

typedef struct lookup lookup_t;

/* ref - missing reference
 * raw_data - true if reference points to raw data
 */
typedef int (*lookup_ref_f)(lookup_t *c,
                            const char *ref,
                            void *data);

/* Initialize a lookup handle
 *
 * - root_ref is optional.  If not specified, will use root ref
 *   specified in namespace.
 */
lookup_t *lookup_create (struct cache *cache,
                         kvsroot_mgr_t *krm,
                         int current_epoch,
                         const char *namespace,
                         const char *root_ref,
                         const char *path,
                         uint32_t rolemask,
                         uint32_t userid,
                         int flags,
                         flux_t *h,
                         void *aux);

/* Destroy a lookup handle */
void lookup_destroy (lookup_t *lh);

/* Determine if lookup handle valid */
bool lookup_validate (lookup_t *lh);

/* Get errnum, should be checked after lookup() returns true to see if
 * an error occurred or not */
int lookup_get_errnum (lookup_t *lh);

/* if user wishes to stall, but needs future knowledge to fail and
 * what error caused the failure.
 */
int lookup_get_aux_errnum (lookup_t *lh);
int lookup_set_aux_errnum (lookup_t *lh, int errnum);

/* Get resulting value of lookup() after lookup() returns true.  The
 * json object returned gives a reference to the caller and must be
 * json_decref()'ed to free memory. */
json_t *lookup_get_value (lookup_t *lh);

/* On lookup stall, get missing reference that should be loaded into
 * the KVS cache via callback function.
 *
 * return -1 in callback to break iteration
 */
int lookup_iter_missing_refs (lookup_t *lh, lookup_ref_f cb, void *data);

/* Convenience function to get current epoch from earlier
 * instantiation.  Convenient if replaying RPC and don't have it
 * presently.
 */
int lookup_get_current_epoch (lookup_t *lh);

/* Convenience function to get namespace from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
const char *lookup_get_namespace (lookup_t *lh);

/* Get auxiliarry data set by user in lookup_create() */
void *lookup_get_aux_data (lookup_t *lh);

/* Set a new current epoch.  Convenience on RPC replays and epoch may
 * be new */
int lookup_set_current_epoch (lookup_t *lh, int epoch);

/* Lookup the key path in the KVS cache starting at root.
 *
 * Return true on success or error.  After return, error should be
 * checked via lookup_get_errnum().  On success, value of resulting
 * lookup can be retrieved via lookup_get_value().
 *
 * Return false if key name cannot be resolved.  Get missing
 * references via lookup_iter_missing_refs().  Caller should then use
 * missing reference to load missing reference into KVS cache via rpc
 * or otherwise.
 */
bool lookup (lookup_t *lh);

#endif /* !_FLUX_KVS_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
