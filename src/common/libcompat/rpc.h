#ifndef _FLUX_JSONC_RPC_H
#define _FLUX_JSONC_RPC_H

#include "src/common/libjson-c/json.h"
#include <czmq.h>
#include <stdint.h>

/* Send a request to 'nodeid' addressed to 'topic'.
 * If 'in' is non-NULL, attach JSON payload, caller retains ownership.
 * Wait for a response.  If response has non-zero errnum, set errno to that
 * value and return -1.  If 'out' is non-NULL, set to JSON payload in response,
 * which caller must free.  It is considered a protocol error if 'out' is
 * set and there is no JSON payload, or 'out' is not set and there is.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_rpc (flux_t *h, uint32_t nodeid, const char *topic,
                   json_object *in, json_object **out)
                   __attribute__ ((deprecated));

#endif /* !_FLUX_JSONC_RPC_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
