#ifndef _FLUX_CORE_REQUEST_H
#define _FLUX_CORE_REQUEST_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>

#include "message.h"

/* Decode a request message.
 * If topic is non-NULL, assign the request topic string.
 * If json_str is non-NULL, assign the payload.  json_str indicates whether
 * payload is expected and it is an EPROTO error if expectations are not met.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_request_decode (const flux_msg_t *msg, const char **topic,
                         const char **json_str);

/* Encode a request message.
 * If json_str is non-NULL, assign the payload.
 */
flux_msg_t *flux_request_encode (const char *topic, const char *json_str);

#endif /* !_FLUX_CORE_REQUEST_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
