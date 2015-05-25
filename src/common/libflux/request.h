#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"

/* Decode a request message.
 * If topic is non-NULL, assign the request topic string.
 * If json_str is non-NULL, assign the payload.  json_str indicates whether
 * payload is expected and it is an EPROTO error if expectations are not met.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode (zmsg_t *zmsg, const char **topic,
                         const char **json_str);

/* Encode a response message.
 * If json_str is non-NULL, assign the payload.
 */
zmsg_t *flux_request_encode (const char *topic, const char *json_str);

/* Send a request.
 * If matchtag is non-NULL, allocate a unique matchtag from the handle
 * and set it in the message bvefore sending, then assign it to *matchtag.
 * The _sendto variant may be used to send a request to a specific node
 * or to FLUX_NODEID_UPSTREAM.  Otherwise FLUX_NODEID_ANY is assumed.
 */
int flux_request_send (flux_t h, uint32_t *matchtag, zmsg_t **zmsg);
int flux_request_sendto (flux_t h, uint32_t *matchtag, zmsg_t **zmsg,
                         uint32_t nodeid);

/* Receive a request.
 */
zmsg_t *flux_request_recv (flux_t h, bool nonblock);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
