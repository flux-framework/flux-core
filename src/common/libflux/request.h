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

/* Receive a request message, blocking until one is available.  If 'nonblock'
 * is true and no request is available, return NULL with errno == EAGAIN.
 * Returns request message on success, or NULL on failure with errno set.
 */
zmsg_t *flux_request_recvmsg (flux_t h, bool nonblock);

/* Send a response message.  This function internally calls zmsg_send(),
 * destroying 'zmsg' on success.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_sendmsg (flux_t h, zmsg_t **zmsg);

/* Receive a response message, blocking until one is available.  If 'nonblock'
 * is true and no request is available, return NULL with errno == EAGAIN.
 * Returns request message on success, or NULL on failure with errno set.
 */
zmsg_t *flux_response_recvmsg (flux_t h, bool nonblock);

/* Give a response message back to the handle.  Responses put back in this
 * way are processed in FIFO order (via flux_response_recvmsg(), reactor
 * callbacks, or rpc calls)  before other incoming messages.  On success,
 * ownership of 'zmsg' is transferred to the handle.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_putmsg (flux_t h, zmsg_t **zmsg);


/* Deprecated interfaces.
 */

int flux_response_recv (flux_t h, json_object **respp, char **tagp, bool nb);
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
