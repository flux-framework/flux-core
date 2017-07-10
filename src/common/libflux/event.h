#ifndef _FLUX_CORE_EVENT_H
#define _FLUX_CORE_EVENT_H

#include <stdbool.h>
#include <stdarg.h>

#include "message.h"

/* Decode an event message.
 * If topic is non-NULL, assign the event topic string.
 * If json_str is non-NULL, assign the payload or set to NULL if none
 * exists.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_decode (const flux_msg_t *msg, const char **topic,
                       const char **json_str);

/* Decode an event message with json payload.  These functions use
 * jansson unpack style variable arguments for decoding the JSON object
 * payload directly.  Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_unpack (const flux_msg_t *msg, const char **topic,
                       const char *fmt, ...);

/* Encode an event message.
 * If json_str is non-NULL, it is copied to the message payload.
 * Returns message or NULL on failure with errno set.
 */
flux_msg_t *flux_event_encode (const char *topic, const char *json_str);

/* Encode an event message with json payload.  These functions use
 * jansson pack style variable arguments for encoding the JSON object
 * payload directly.  Returns message or NULL on failure with errno set.
 */
flux_msg_t *flux_event_encodef (const char *topic, const char *fmt, ...);

#endif /* !FLUX_CORE_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
