#ifndef _FLUX_CORE_EVENT_H
#define _FLUX_CORE_EVENT_H

#include "message.h"
#include "future.h"

#ifdef __cplusplus
extern "C" {
#endif

enum event_flags {
    FLUX_EVENT_PRIVATE = 1,
};

/* Decode an event message with optional string payload.
 * If topic is non-NULL, assign the event topic string.
 * If s is non-NULL, assign string payload or set to NULL if none
 * exists.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_decode (const flux_msg_t *msg, const char **topic,
                       const char **s);

/* Decode an event message with required JSON payload.  These functions use
 * jansson unpack style variable arguments for decoding the JSON object
 * payload directly.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_unpack (const flux_msg_t *msg, const char **topic,
                       const char *fmt, ...);

/* Encode an event message with optinal string payload.
 * If s is non-NULL, it is copied to the message payload.
 * Returns message or NULL on failure with errno set.
 */
flux_msg_t *flux_event_encode (const char *topic, const char *s);

/* Encode an event message with JSON payload.  These functions use
 * jansson pack style variable arguments for encoding the JSON object
 * payload directly.  Returns message or NULL on failure with errno set.
 */
flux_msg_t *flux_event_pack (const char *topic, const char *fmt, ...);

/* Encode an event message with raw payload.
 */
flux_msg_t *flux_event_encode_raw (const char *topic,
                                   const void *data, int len);

/* Decode an event message, with optional raw payload.
 * If topic is non-NULL, assign the event topic string.
 * Data and len must be non-NULL and will be assigned the payload and length.
 * If there is no payload, they will be assigned NULL and zero.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_decode_raw (const flux_msg_t *msg, const char **topic,
                           const void **data, int *len);

/* Publish an event with optional string payload.
 * The future is fulfilled once the event has been assigned a sequence number,
 * and does not indicate that the event has yet reached all subscribers.
 */
flux_future_t *flux_event_publish (flux_t *h,
                                   const char *topic, int flags,
                                   const char *s);

/* Publish an event with JSON payload.
 */
flux_future_t *flux_event_publish_pack (flux_t *h,
                                        const char *topic, int flags,
                                        const char *fmt, ...);

/* Publish an event with optinal raw paylaod.
 */
flux_future_t *flux_event_publish_raw (flux_t *h,
                                       const char *topic, int flags,
                                       const void *data, int len);

/* Obtain the event sequence number from the fulfilled
 * flux_event_publish() future.
 */
int flux_event_publish_get_seq (flux_future_t *f, int *seq);

#ifdef __cplusplus
}
#endif

#endif /* !FLUX_CORE_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
