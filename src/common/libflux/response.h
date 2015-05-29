#ifndef _FLUX_CORE_RESPONSE_H
#define _FLUX_CORE_RESPONSE_H

#include <stdbool.h>
#include <stdarg.h>

#include "message.h"
#include "handle.h"

/* Decode a response message.
 * If topic is non-NULL, assign the response topic string.
 * If json_str is non-NULL, assign the payload.  json_str indicates whether
 * payload is expected and it is an EPROTO error if expectations are not met.
 * If response includes a nonzero errnum, errno is set to the errnum value
 * and -1 is returned with no assignments to topic or json_str.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode (zmsg_t *zmsg, const char **topic,
                          const char **json_str);


/* Encode a response message, copying the matchtag, topic string, and
 * route stack from the supplied request message.
 * If json_str is non-NULL, it is copied to the response message payload.
 * Use the _err variant to return a UNIX errno value to the caller.
 * (_ok with payload of NULL == _err with errnum of 0)
 * Returns message on success, or NULL on failure with errno set.
 */
zmsg_t *flux_response_encode_ok (zmsg_t *request, const char *json_str);
zmsg_t *flux_response_encode_err (zmsg_t *request, int errnum);

zmsg_t *flux_response_encode (const char *topic, int errnum,
                              const char *json_str);

/* Send/receive response
 */
int flux_response_send (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_response_recv (flux_t h, uint32_t matchtag, bool nonblock);

#endif /* !_FLUX_CORE_RESPONSE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
