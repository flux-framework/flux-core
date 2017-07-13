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
 */
bool json_compare (json_t *o1, json_t *o2);

/* Calculate hash of a json object
 */
int json_hash (const char *hash_name, json_t *o, href_t ref);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
