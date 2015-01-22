#ifndef _FLUX_CORE_RPC_H
#define _FLUX_CORE_RPC_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"
#include "request.h"

/* Send a request to 'nodeid' (may be FLUX_NODEID_ANY) addressed to 'topic'.
 * If 'in' is non-NULL, attach JSON payload, caller retains ownership.
 * Wait for a response.  If response has non-zero errnum, set errno to that
 * value and return -1.  If 'out' is non-NULL, set to JSON payload in response,
 * which caller must free.  It is considered a protocol error if 'out' is
 * set and there is no JSON payload, or 'out' is not set and there is.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_rpc (flux_t h, uint32_t nodeid, const char *topic,
                   json_object *in, json_object **out);

/* Send a request to each node in 'nodeset', then collect responses,
 * calling 'cb' for each one (if 'cb' is non-NULL).
 * Returns 0 on success, -1 on failure with errno set.
 * If there are multiple failures, their greatest errno is returned.
 */
typedef int (flux_multrpc_f)(uint32_t nodeid, uint32_t errnum,
                             json_object *out, void *arg);
int flux_json_multrpc (flux_t h, const char *nodeset, int fanout,
                       const char *topic, json_object *in,
                       flux_multrpc_f cb, void *arg);

/**
 ** Deprecated interfaces.
 **/

json_object *flux_rpc (flux_t h, json_object *in, const char *fmt, ...);
json_object *flux_rank_rpc (flux_t h, int rank,
                            json_object *in, const char *fmt, ...);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
