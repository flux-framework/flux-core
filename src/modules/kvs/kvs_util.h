#ifndef _FLUX_KVS_UTIL_H
#define _FLUX_KVS_UTIL_H
#include <jansson.h>

#include "src/common/libutil/tstat.h"
#include "waitqueue.h"
#include "types.h"

/* Copy element wise a json directory object into a new json object.
 */
json_t *kvs_util_json_copydir (json_t *dir);

/* Get compact string representation of json object, or json null
 * object if o is NULL.  Use this function for consistency, especially
 * when dealing with data that may be hashed via json_hash().
 *
 * Returns NULL on error
 */
char *kvs_util_json_dumps (json_t *o);

/* Calculate hash of a json object
 *
 * Returns -1 on error, 0 on success
 */
int kvs_util_json_hash (const char *hash_name, json_t *o, href_t ref);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
