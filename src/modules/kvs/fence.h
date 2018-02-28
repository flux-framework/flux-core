#ifndef _FLUX_KVS_FENCE_H
#define _FLUX_KVS_FENCE_H

#include <czmq.h>
#include <jansson.h>

typedef struct fence_mgr fence_mgr_t;

typedef struct fence fence_t;

typedef int (*fence_itr_f)(fence_t *f, void *data);

typedef int (*fence_msg_cb)(fence_t *f, const flux_msg_t *req, void *data);

/*
 * fence_mgr_t API
 */

/* flux_t is optional, if NULL logging will go to stderr */
fence_mgr_t *fence_mgr_create (void);

void fence_mgr_destroy (fence_mgr_t *fm);

/* Add fence into the fence manager */
int fence_mgr_add_fence (fence_mgr_t *fm, fence_t *f);

/* Lookup a fence previously stored via fence_mgr_add_fence(), via name */
fence_t *fence_mgr_lookup_fence (fence_mgr_t *fm, const char *name);

/* Iterate through all fences */
int fence_mgr_iter_fences (fence_mgr_t *fm, fence_itr_f cb, void *data);

/* remove a fence from the fence manager */
int fence_mgr_remove_fence (fence_mgr_t *fm, const char *name);

/* Get count of fences stored */
int fence_mgr_fences_count (fence_mgr_t *fm);

/*
 * fence_t API
 */

fence_t *fence_create (const char *name, int nprocs, int flags);

void fence_destroy (fence_t *f);

/* if number of calls to fence_add_request_data() is == nprocs */
bool fence_count_reached (fence_t *f);

const char *fence_get_name (fence_t *f);
int fence_get_nprocs (fence_t *f);
int fence_get_flags (fence_t *f);

json_t *fence_get_json_ops (fence_t *f);

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

/* convenience processing flag
 */
bool fence_get_processed (fence_t *f);
void fence_set_processed (fence_t *f, bool p);

#endif /* !_FLUX_KVS_FENCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

