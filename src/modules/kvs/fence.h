#ifndef _FLUX_KVS_FENCE_H
#define _FLUX_KVS_FENCE_H

#include <czmq.h>

#include "src/common/libutil/shortjson.h"

typedef struct {
    int nprocs;
    int count;
    zlist_t *requests;
    json_object *ops;
    json_object *names;
    int flags;
} fence_t;

fence_t *fence_create (const char *name, int nprocs, int flags);

void fence_destroy (fence_t *f);

/* if number of calls to fence_add_request_data() is == nprocs */
bool fence_count_reached (fence_t *f);

int fence_get_flags (fence_t *f);
void fence_set_flags (fence_t *f, int flags);

json_object *fence_get_json_ops (fence_t *f);

json_object *fence_get_json_names (fence_t *f);

/* fence_add_request_data() should be called with data on each
 * request, even if ops is NULL
 */
int fence_add_request_data (fence_t *f, json_object *ops);

/* copy the request message into the fence, where it can be retrieved
 * later.
 */
int fence_add_request_copy (fence_t *f, const flux_msg_t *request);

/* Merge src ops & names into dest ops & names
 * - return 1 on merge success, 0 on no-merge
 */
int fence_merge (fence_t *dest, fence_t *src);

#endif /* !_FLUX_KVS_FENCE_H */
