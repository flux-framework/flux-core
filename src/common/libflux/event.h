#ifndef _FLUX_CORE_EVENT_H
#define _FLUX_CORE_EVENT_H

#include <json.h>
#include <stdbool.h>
#include <stdarg.h>
#include <czmq.h>

#include "handle.h"

/* Decode an event message.
 * If topic is non-NULL, assign the event topic string.
 * If json_str is non-NULL, assign the payload.  json_str indicates whether
 * payload is expected and it is an EPROTO error if they don't match.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_event_decode (zmsg_t *zmsg, const char **topic, const char **json_str);

/* Encode an event message.
 * If json_str is non-NULL, it is copied to the message payload.
 * Returns message or NULL on failure with errno set.
 */
zmsg_t *flux_event_encode (const char *topic, const char *json_str);

/* Decode event message, setting 'in' to JSON payload.
 * If payload is missing, fail with errno == EPROTO.
 * Returns 0 on success, or -1 on failure with errno set.
 */
int flux_json_event_decode (zmsg_t *zmsg, json_object **in);

/* Send/receive events
 */
int flux_event_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_event_recv (flux_t h, json_object **respp, char **topic, bool nb);

#endif /* !FLUX_CORE_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
