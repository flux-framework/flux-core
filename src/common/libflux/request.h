#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"

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
int flux_json_request (flux_t h, uint32_t nodeid, uint32_t matchtag,
                       const char *topic, json_object *in);

/* Convert 'zmsg' request into a response and send it.  'zmsg' is destroyed
 * on success.  Attach JSON payload 'out' (caller retains owenrship).
 * The original payload in the request, if any, is destroyed.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_respond (flux_t h, json_object *out, zmsg_t **zmsg);

/* Convert 'zmsg' request into a response and send it.  'zmsg' is destroyed
 * on success.  Set errnum in response to 'errnum' (may be zero).
 * The original payload in the request, if any, is destroyed.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_err_respond (flux_t h, int errnum, zmsg_t **zmsg);

/* Decode request message, setting 'in' to JSON payload.
 * If payload is missing, fail with errno == EPROTO.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_request_decode (zmsg_t *zmsg, json_object **in);

/* Decode response message, setting 'out' to JSON payload.
 * If payload is missing, fail with errno == EPROTO.
 * If errnum is nonzero in response, fail with errno == errnum.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_response_decode (zmsg_t *zmsg, json_object **out);

/* Decode response message with no payload.
 * If there is a payload, fail with errno == EPROTO.
 * If errnum is nonzero in response, fail with errno == errnum.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode (zmsg_t *zmsg);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
