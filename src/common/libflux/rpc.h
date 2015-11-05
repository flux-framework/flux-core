#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <stdbool.h>

#include "handle.h"

enum {
    FLUX_RPC_NORESPONSE = 1,
};

typedef struct flux_rpc_struct flux_rpc_t;
typedef void (*flux_then_f)(flux_rpc_t *rpc, void *arg);

/* Send an RPC request to 'nodeid' with optional json payload,
 * and return a flux_rpc_t object to allow the response to be handled.
 * On failure return NULL with errno set.
 */
flux_rpc_t *flux_rpc (flux_t h, const char *topic, const char *json_str,
                      uint32_t nodeid, int flags);

/* Send an RPC request to 'nodeid' with optional raw paylaod,
 * and return a flux_rpc_t object to allow the response to be handled.
 * On failure return NULL with errno set.
 */
flux_rpc_t *flux_rpc_raw (flux_t h, const char *topic,
                          const void *data, int len,
                          uint32_t nodeid, int flags);

/* Destroy an RPC, invalidating previous payload returned by flux_rpc_get().
 */
void flux_rpc_destroy (flux_rpc_t *rpc);

/* Returns true if flux_rpc_get() can be called without blocking.
 */
bool flux_rpc_check (flux_rpc_t *rpc);

/* Wait for a response if necessary, then decode it.
 * Any returned 'json_str' payload is valid until the next get/check call.
 * If 'nodeid' is non-NULL, the nodeid that the request was sent to is returned.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_rpc_get (flux_rpc_t *rpc, uint32_t *nodeid, const char **json_str);

/* Wait for a response if necessary, then decode it.
 * Any returned 'data' payload is valid until the next get/check call.
 * If 'nodeid' is non-NULL, the nodeid that the request was sent to is returned.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_rpc_get_raw (flux_rpc_t *rpc, uint32_t *nodeid, void *data, int *len);

/* Arrange for reactor to handle response and call 'cb' continuation function
 * when a response is received.  The function must call flux_rpc_get().
 * A second call to flux_rpc_then() overwrites the internal (cb, arg) refs.
 * Call with NULL to disable the reactor message handler for this RPC.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_rpc_then (flux_rpc_t *rpc, flux_then_f cb, void *arg);

/* Send an RPC request to 'nodeset' and return a flux_rpc_t object
 * to allow responses to be handled.  "all" is a valid shorthand for
 * all ranks in the comms session.  On failure return NULL with errno set.
 */
flux_rpc_t *flux_rpc_multi (flux_t h, const char *topic, const char *json_str,
                            const char *nodeset, int flags);

/* Returns true when all responses to an RPC have been received and consumed.
 */
bool flux_rpc_completed (flux_rpc_t *rpc);

/* Helper functions for extending flux_rpc_t.
 */
const char *flux_rpc_type_get (flux_rpc_t *rpc);
void flux_rpc_type_set (flux_rpc_t *rpc, const char *type);
void *flux_rpc_aux_get (flux_rpc_t *rpc);
void flux_rpc_aux_set (flux_rpc_t *rpc, void *aux, flux_free_f destroy);

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
