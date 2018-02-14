#ifndef _FLUX_KVS_FENCE_H
#define _FLUX_KVS_FENCE_H

#include <czmq.h>
#include <jansson.h>

typedef struct fence fence_t;

typedef int (*fence_msg_cb)(fence_t *f, const flux_msg_t *req, void *data);

fence_t *fence_create (const char *name, int nprocs, int flags);

void fence_destroy (fence_t *f);

/* if number of calls to fence_add_request_data() is == nprocs */
bool fence_count_reached (fence_t *f);

int fence_get_nprocs (fence_t *f);
int fence_get_flags (fence_t *f);

json_t *fence_get_json_ops (fence_t *f);

json_t *fence_get_json_names (fence_t *f);

/* fence_add_request_ops() should be called with ops on each
 * request, even if ops is NULL
 */
int fence_add_request_ops (fence_t *f, json_t *ops);

/* copy the request message into the fence, where it can be retrieved
 * later.
 */
int fence_add_request_copy (fence_t *f, const flux_msg_t *request);

/* Call callback for each request message copy stored internally via
 * fence_add_request_copy().
 *
 * If cb returns < 0 on a message, this function was quit and return
 * -1.
 */
int fence_iter_request_copies (fence_t *f, fence_msg_cb cb, void *data);

/* Merge src ops & names into dest ops & names
 * - return 1 on merge success, 0 on no-merge, -1 on error
 */
int fence_merge (fence_t *dest, fence_t *src);

/* Auxiliary convenience data
 */
int fence_get_aux_int (fence_t *f);
void fence_set_aux_int (fence_t *f, int n);

#endif /* !_FLUX_KVS_FENCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

