#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"

/* Request and response messages are constructed according to Flux RFC 3.
 * https://github.com/garlick/rfc/blob/master/spec_3.adoc
 * See also message.h.
 */

/* Send a request message.  This function internally calls zmsg_send(),
 * destroying 'zmsg' on success.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_sendmsg (flux_t h, zmsg_t **zmsg);

/* Receive a request message, blocking until one is available.
 * If 'nonblock' and none is available, return NULL with errno == EAGAIN.
 * Returns message on success, or NULL on failure with errno set.
 */
zmsg_t *flux_request_recvmsg (flux_t h, bool nonblock);

/* Send a response message.  This function internally calls zmsg_send(),
 * destroying 'zmsg' on success.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_sendmsg (flux_t h, zmsg_t **zmsg);

/* Receive a response message, blocking until one is available.
 * If 'nonblock' and none is available, return NULL with errno == EAGAIN.
 * Returns message on success, or NULL on failure with errno set.
 */
zmsg_t *flux_response_recvmsg (flux_t h, bool nonblock);

/* Put a response message in the handle's inbound message queue for processing
 * in FIFO order, before other unprocessed messages.  The handle will become
 * ready and the response will be returned by a call to flux_response_recvmsg()
 * or similar.
 * On success, ownership of 'zmsg' is transferred to the handle.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_putmsg (flux_t h, zmsg_t **zmsg);

/* Send a request to 'nodeid' (may be FLUX_NODEID_ANY) addressed to 'topic'.
 * If 'in' is non-NULL, attach JSON payload, caller retains ownership.
 * Do not wait for a response.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_request (flux_t h, uint32_t nodeid, const char *topic,
                       json_object *in);

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

/* FIXME:  rework?
 */
int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb);

/* Deprecated interfaces.
 */

int flux_respond (flux_t h, zmsg_t **request, json_object *response);
int flux_respond_errnum (flux_t h, zmsg_t **request, int errnum);

int flux_request_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_rank_request_send (flux_t h, int rank,
                            json_object *request, const char *fmt, ...);
json_object *flux_rpc (flux_t h, json_object *in, const char *fmt, ...);
json_object *flux_rank_rpc (flux_t h, int rank,
                            json_object *in, const char *fmt, ...);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
