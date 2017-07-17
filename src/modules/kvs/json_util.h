#ifndef _FLUX_KVS_JSON_UTIL_H
#define _FLUX_KVS_JSON_UTIL_H
#include <jansson.h>

#include "src/common/libutil/tstat.h"
#include "waitqueue.h"
#include "types.h"

/* Copy element wise a json directory object into a new json object.
 */
json_t *json_object_copydir (json_t *dir);

/* Compare two json objects, return true if same, false if not
 *
 * Note that passing in NULL for o1, o2, or both will always result in
 * false.
 */
bool json_compare (json_t *o1, json_t *o2);

/* Get compact string representation of json object, or json null
 * object if o is NULL.  Use this function for consistency, especially
 * when dealing with data that may be hashed via json_hash().
 */
char *json_strdump (json_t *o);

/* Calculate hash of a json object
 */
int json_hash (const char *hash_name, json_t *o, href_t ref);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
