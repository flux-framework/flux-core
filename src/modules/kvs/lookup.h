/************************************************************\
 * Copyright 2015 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_KVS_LOOKUP_H
#define _FLUX_KVS_LOOKUP_H

#include <flux/core.h>
#include "cache.h"
#include "kvsroot.h"

typedef struct lookup lookup_t;

typedef enum {
    LOOKUP_PROCESS_ERROR = 1,
    LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE = 2,
    LOOKUP_PROCESS_LOAD_MISSING_REFS = 3,
    LOOKUP_PROCESS_FINISHED = 4,
} lookup_process_t;

/* ref - missing reference
 * raw_data - true if reference points to raw data
 */
typedef int (*lookup_ref_f) (lookup_t *c, const char *ref, void *data);

/* Initialize a lookup handle
 *
 * - atleast one of namespace & root_ref is required.
 * - root_seq is not used and is solely used for convenience being
 *   passed alongside root_ref.  Can be retrieved later with
 *   lookup_get_root_seq().  Will not be stored if root_ref is NULL.
 */
lookup_t *lookup_create (struct cache *cache,
                         kvsroot_mgr_t *krm,
                         int current_epoch,
                         const char *ns,
                         const char *root_ref,
                         int root_seq,
                         const char *path,
                         uint32_t rolemask,
                         uint32_t userid,
                         int flags,
                         flux_t *h);

/* Destroy a lookup handle */
void lookup_destroy (lookup_t *lh);

/* Get errnum, should be checked after lookup() returns
 * LOOKUP_PROCESS_ERROR */
int lookup_get_errnum (lookup_t *lh);

/* if user wishes to stall, but needs future knowledge to fail and
 * what error caused the failure.
 */
int lookup_get_aux_errnum (lookup_t *lh);
int lookup_set_aux_errnum (lookup_t *lh, int errnum);

/* Get resulting value of lookup() after lookup() returns
 * LOOKUP_PROCESS_FINISHED.  The json object returned gives a
 * reference to the caller and must be json_decref()'ed to free
 * memory. */
json_t *lookup_get_value (lookup_t *lh);

/* On lookup stall b/c of missing reference(s), get missing reference
 * that should be loaded into the KVS cache via callback function.
 *
 * return -1 in callback to break iteration
 */
int lookup_iter_missing_refs (lookup_t *lh, lookup_ref_f cb, void *data);

/* On lookup stall b/c of missing namespace, get missing namespace
 * returned by this function.
 *
 * returns NULL on error
 */
const char *lookup_missing_namespace (lookup_t *lh);

/* Convenience function to get current epoch from earlier
 * instantiation.  Convenient if replaying RPC and don't have it
 * presently.
 */
int lookup_get_current_epoch (lookup_t *lh);

/* Convenience function to get namespace from earlier instantiation.
 * Convenient if replaying RPC and don't have it presently.
 */
const char *lookup_get_namespace (lookup_t *lh);

/* Convenience functions to get root ref & seq used in lookup.
 * root_ref will be the root_ref passed in via lookup_create() or the
 * root_ref used from the namespace.  The root_seq is only if the
 * root_ref was from a namespace.  Note that the values are not valid
 * unless the lookup completes (LOOKUP_PROCESS_FINISED).
 */
const char *lookup_get_root_ref (lookup_t *lh);
int lookup_get_root_seq (lookup_t *lh);

/* Set a new current epoch.  Convenience on RPC replays and epoch may
 * be new */
int lookup_set_current_epoch (lookup_t *lh, int epoch);

/* Lookup the key path in the KVS cache starting at root.
 *
 * Returns LOOKUP_PROCESS_ERROR on error,
 * LOOKUP_PROCESS_LOAD_MISSING_NAMESPACE on stall & load missing namespace,
 * LOOKUP_PROCESS_LOAD_MISSING_REFS on stall & load missing references,
 * LOOKUP_PROCESS_FINISHED on all done and success.
 *
 * On error, error should be retrieved via lookup_get_errnum().
 *
 * On stall & load missing namespace, get missing namespace via
 * lookup_missing_namespace().  Caller should then use missing
 * namespace to load missing namespace into kvsroot_mgr_t.
 *
 * On stall & load missing references, get missing references via
 * lookup_iter_missing_refs().  Caller should then use missing
 * reference to load missing reference into KVS cache via rpc or
 * otherwise.
 *
 * On success, value of resulting lookup can be retrieved via
 * lookup_get_value().
 *
 * Return false if key name cannot be resolved.
 */
lookup_process_t lookup (lookup_t *lh);

#endif /* !_FLUX_KVS_LOOKUP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
