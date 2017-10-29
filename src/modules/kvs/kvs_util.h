#ifndef _FLUX_KVS_UTIL_H
#define _FLUX_KVS_UTIL_H
#include <jansson.h>

#include "src/common/libutil/tstat.h"
#include "waitqueue.h"
#include "types.h"

/* Get compact string representation of json object, or json null
 * object if o is NULL.  Use this function for consistency, especially
 * when dealing with data that may be hashed via kvs_util_json_hash().
 *
 * Returns NULL on error
 */
char *kvs_util_json_dumps (json_t *o);

/* returns 0 on success, -1 on failure */
int kvs_util_json_encoded_size (json_t *o, size_t *size);

/* Calculate hash of a json object
 *
 * Returns -1 on error, 0 on success
 */
int kvs_util_json_hash (const char *hash_name, json_t *o, href_t ref);

/* Normalize a KVS key
 * Returns new key string (caller must free), or NULL with errno set.
 * On success, 'want_directory' is set to true if key had a trailing
 * path separator.
 */
char *kvs_util_normalize_key (const char *key, bool *want_directory);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
