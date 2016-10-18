#ifndef _FLUX_CORE_RESPONSE_H
#define _FLUX_CORE_RESPONSE_H

#include <stdbool.h>
#include <stdarg.h>

#include "message.h"
#include "handle.h"

/* Decode a response message, with optional json payload.
 * If topic is non-NULL, assign the response topic string.
 * If json_str is non-NULL, assign the payload.  This argument indicates whether
 * payload is expected and it is an EPROTO error if expectations are not met.
 * If response includes a nonzero errnum, errno is set to the errnum value
 * and -1 is returned with no assignments to topic or json_str.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode (const flux_msg_t *msg, const char **topic,
                          const char **json_str);

/* Decode a response message, with optional raw payload.
 * If topic is non-NULL, assign the response topic string.
 * Data and len must be non-NULL and will be assigned the payload and length.
 * If there is no payload, they will be assigned NULL and zero.
 * If response includes a nonzero errnum, errno is set to the errnum value
 * and -1 is returned with no assignments to topic or json_str.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode_raw (const flux_msg_t *msg, const char **topic,
                              void *data, int *len);

flux_msg_t *flux_response_encode (const char *topic, int errnum,
                                  const char *json_str);
/* Encode a response message with optional raw payload.
 */
flux_msg_t *flux_response_encode_raw (const char *topic, int errnum,
                                      const void *data, int len);

/* Create a response to the provided request message with optional json payload.
 * If errnum is nonzero, payload argument is ignored.
 * All errors in this function are fatal - see flux_fatal_set().
 */
int flux_respond (flux_t *h, const flux_msg_t *request,
                  int errnum, const char *json_str);

/* Create a response to the provided request message with json payload, using
 * jansson pack style variable arguments for encoding the JSON object
 * payload directly.
 * All errors in this function are fatal - see flux_fatal_set().
 */
int flux_respondf (flux_t *h, const flux_msg_t *request,
                   const char *fmt, ...);


/* Create a response to the provided request message with optional raw payload.
 * If errnum is nonzero, payload argument is ignored.
 * All errors in this function are fatal - see flux_fatal_set().
 */
int flux_respond_raw (flux_t *h, const flux_msg_t *request,
                      int errnum, const void *data, int len);

#endif /* !_FLUX_CORE_RESPONSE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
