#ifndef _FLUX_CORE_EVENT_H
#define _FLUX_CORE_EVENT_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>

#include "handle.h"

/* Send/receive events
 * - an event consists of a tag frame and an optional JSON frame
 * - flux_event_sendmsg() frees '*zmsg' and sets it to NULL on success.
 * - int-returning functions return 0 on success, -1 on failure with errno set.
 * - pointer-returning functions return NULL on failure with errno set.
 * - topics are period-delimited strings following 0MQ subscription semantics
 */
int flux_event_sendmsg (flux_t h, zmsg_t **zmsg);
zmsg_t *flux_event_recvmsg (flux_t h, bool nonblock);
int flux_event_send (flux_t h, json_object *request, const char *fmt, ...);
int flux_event_recv (flux_t h, json_object **respp, char **tagp, bool nb);
int flux_event_subscribe (flux_t h, const char *topic);
int flux_event_unsubscribe (flux_t h, const char *topic);

#endif /* !FLUX_CORE_EVENT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
