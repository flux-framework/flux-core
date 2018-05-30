#ifndef _FLUX_CORE_RESPONSE_H
#define _FLUX_CORE_RESPONSE_H

#include "message.h"
#include "handle.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a response message, with optional string payload.
 * If topic is non-NULL, assign the response topic string.
 * If s is non-NULL, assign the string payload if one exists or set to
 * NULL is none exists.  If response includes a nonzero errnum, errno
 * is set to the errnum value and -1 is returned with no assignments
 * to topic or s.  Returns 0 on success, or -1 on failure with
 * errno set.
 */
int flux_response_decode (const flux_msg_t *msg, const char **topic,
                          const char **s);

/* Decode a response message, with optional raw payload.
 * If topic is non-NULL, assign the response topic string.
 * Data and len must be non-NULL and will be assigned the payload and length.
 * If there is no payload, they will be assigned NULL and zero.
 * If response includes a nonzero errnum, errno is set to the errnum value
 * and -1 is returned with no assignments to topic, data, or len.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode_raw (const flux_msg_t *msg, const char **topic,
                              const void **data, int *len);

/* If failed response includes an error string payload, assign to 'errstr',
 * otherwise fail.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_response_decode_error (const flux_msg_t *msg, const char **errstr);


/* Encode a message with optional string payload 's'.
 */
flux_msg_t *flux_response_encode (const char *topic, const char *s);

/* Encode a response message with optional raw payload.
 */
flux_msg_t *flux_response_encode_raw (const char *topic,
                                      const void *data, int len);

/* Encode an error response with 'errnum' (must be nonzero) and
 * if non-NULL, an error string payload.
 */
flux_msg_t *flux_response_encode_error (const char *topic, int errnum,
                                        const char *errstr);

/* Create a response to the provided request message with optional
 * string payload.
 * If errnum is nonzero, payload argument is ignored.
 */
int flux_respond (flux_t *h, const flux_msg_t *request,
                  int errnum, const char *s);

/* Create a response to the provided request message with json payload, using
 * jansson pack style variable arguments for encoding the JSON object
 * payload directly.
 */
int flux_respond_pack (flux_t *h, const flux_msg_t *request,
                       const char *fmt, ...);


/* Create a response to the provided request message with optional raw payload.
 */
int flux_respond_raw (flux_t *h, const flux_msg_t *request,
                      const void *data, int len);

/* Create an error response to the provided request message with optional
 * printf-style error string payload if 'fmt' is non-NULL.
 */
int flux_respond_error (flux_t *h, const flux_msg_t *request,
                        int errnum, const char *fmt, ...)
                        __attribute__ ((format (printf, 4, 5)));


#ifdef __cplusplus
}
#endif

#endif /* !_FLUX_CORE_RESPONSE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
