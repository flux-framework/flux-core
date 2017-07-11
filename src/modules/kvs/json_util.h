#ifndef _FLUX_KVS_JSON_UTIL_H
#define _FLUX_KVS_JSON_UTIL_H

#include "src/common/libutil/tstat.h"
#include "waitqueue.h"

/* Copy element wise a json directory object into a new json object.
 */
json_object *json_object_copydir (json_object *dir);

/* Compare two json objects, return true if same, false if not
 */
bool json_compare (json_object *o1, json_object *o2);

#endif  /* !_FLUX_KVS_JSON_UTIL_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
