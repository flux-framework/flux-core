#include "cache.h"

/* Lookup the key path in the KVS cache starting at root.
 *
 * Return true on success or error, error code is returned in ep and
 * should be checked upon return.
 *
 * Return false if key name cannot be resolved.  Return missing
 * reference in missing_ref, which caller should then use to load missing
 * reference into KVS cache via rpc or otherwise.
 */
bool lookup (struct cache *cache, int current_epoch, json_object *root,
             const char *rootdir, const char *path, int flags,
             json_object **valp, const char **missing_ref, int *ep);

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
