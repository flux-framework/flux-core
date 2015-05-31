#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"
#include "request.h"

enum {
    FLUX_RPC_NORESPONSE = 1,
};

typedef struct flux_rpc_struct *flux_rpc_t;
typedef void (*flux_then_f)(flux_rpc_t rpc, void *arg);

/* Send an RPC request to 'nodeid', and return a flux_rpc_t object
 * to allow the response to be handled.  On failure return NULL with errno set.
 */
flux_rpc_t flux_rpc (flux_t h, const char *topic, const char *json_str,
                     uint32_t nodeid, int flags);
/* Destroy an RPC, invalidating previous payload returned by flux_rpc_get().
 */
void flux_rpc_destroy (flux_rpc_t rpc);

/* Returns true if flux_rpc_get() can be called without blocking.
 */
bool flux_rpc_check (flux_rpc_t rpc);

/* Wait for a response if necessary, then decode it.
 * Any returned 'json_str' payload is valid until the next get/check call.
 * If 'nodeid' is non-NULL, the nodeid that the request was sent to is returned.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_rpc_get (flux_rpc_t rpc, uint32_t *nodeid, const char **json_str);

/* Arrange for reactor to handle response and call 'cb' continuation function
 * when a response is received.  The function must call flux_rpc_get().
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_rpc_then (flux_rpc_t rpc, flux_then_f cb, void *arg);

/* Send an RPC request to 'nodeset' and return a flux_rpc_t object
 * to allow responses to be handled.  "all" is a valid shorthand for
 * all ranks in the comms session.  On failure return NULL with errno set.
 */
flux_rpc_t flux_multrpc (flux_t h, const char *topic, const char *json_str,
                         const char *nodeset, int flags);

/* Returns true when all responses to an RPC have been received and consumed.
 */
bool flux_rpc_completed (flux_rpc_t rpc);

#endif /* !_FLUX_CORE_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
