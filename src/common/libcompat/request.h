#ifndef _FLUX_JSONC_REQUEST_H
#define _FLUX_JSONC_REQUEST_H

#include "src/common/libjson-c/json.h"
#include <czmq.h>
#include <stdint.h>

#define flux_json_request           compat_request
#define flux_json_respond           compat_respond

/* Request and response messages are constructed according to Flux RFC 3.
 * https://github.com/flux-framework/rfc/blob/master/spec_3.adoc
 * See also message.h.
 */

/* Send a request to 'nodeid' addressed to 'topic'.
 * If 'in' is non-NULL, attach JSON payload, caller retains ownership.
 * Set 'matchtag' to FLUX_MATCHTAG_NONE to disable tag matching, or
 * allocate/free one from the handle with flux_matchtag_alloc()/_free().
 * This function does not wait for a response message.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_request (flux_t *h, uint32_t nodeid, uint32_t matchtag,
                       const char *topic, json_object *in)
                       __attribute__ ((deprecated));

/* Convert 'msg' request into a response and send it.  'zmsg' is destroyed
 * on success.  Attach JSON payload 'out' (caller retains owenrship).
 * The original payload in the request, if any, is destroyed.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_respond (flux_t *h, json_object *out, flux_msg_t **msg)
                       __attribute__ ((deprecated));

#endif /* !_FLUX_JSONC_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
