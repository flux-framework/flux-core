#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>
#include <stdint.h>

enum {
    FLUX_MSGTYPE_REQUEST    = 0x01,
    FLUX_MSGTYPE_RESPONSE   = 0x02,
    FLUX_MSGTYPE_EVENT      = 0x04,
    FLUX_MSGTYPE_KEEPALIVE  = 0x08,
    FLUX_MSGTYPE_ANY        = 0x0f,
    FLUX_MSGTYPE_MASK       = 0x0f,
};

/* These are used internally.
 */
enum {
    FLUX_MSGFLAG_TOPIC      = 0x01,	/* message has topic string */
    FLUX_MSGFLAG_PAYLOAD    = 0x02,	/* message has payload */
    FLUX_MSGFLAG_JSON       = 0x04,	/* message payload is JSON */
    FLUX_MSGFLAG_RTE        = 0x08,	/* message has routing delimiter */
};

/* Create a new Flux message.
 * Returns new message or null on failure, with errno set (e.g. ENOMEM, EINVAL)
 * Caller must destroy message with zmsg_destroy() or equivalent.
 */
zmsg_t *flux_msg_create (int type);

/* Get/set/cmp message topic string.
 * Set function adds/deletes topic frame as needed.
 * cmp_topic returns true if message topic string and provided are identical
 */
int flux_msg_set_topic (zmsg_t *zmsg, const char *topic);
int flux_msg_get_topic (zmsg_t *zmsg, char **topic);
bool flux_msg_streq_topic (zmsg_t *zmsg, const char *topic);
bool flux_msg_strneq_topic (zmsg_t *zmsg, const char *topic, size_t n);

/* Get/set payload.
 * Set function adds/deletes payload frame as needed (caller retains ownership)
 * Get_payload returns pointer to zmsg-owned buf.
 * Get_json returns JSON object that caller must free.
 */
int flux_msg_set_payload (zmsg_t *zmsg, void *buf, int size);
int flux_msg_get_payload (zmsg_t *zmsg, void **buf, int *size);
int flux_msg_set_payload_json (zmsg_t *zmsg, json_object *o);
int flux_msg_get_payload_json (zmsg_t *zmsg, json_object **o);

/* Get/set nodeid (request only)
 */
#define FLUX_NODEID_ANY	(~(uint32_t)0)
int flux_msg_set_nodeid (zmsg_t *zmsg, uint32_t nodeid);
int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nodeid);

/* Get/set errnum (response only)
 */
int flux_msg_set_errnum (zmsg_t *zmsg, int errnum);
int flux_msg_get_errnum (zmsg_t *zmsg, int *errnum);

/* Get/set sequence number (event only)
 */
int flux_msg_set_seq (zmsg_t *zmsg, uint32_t seq);
int flux_msg_get_seq (zmsg_t *zmsg, uint32_t *seq);

/* Get/set presence of routing delimiter.
 * This is a no-op if message is already in the desired state.
 * Any routing identity frames are discarded if the flag is being cleared.
 * Only the delimiter is added if the flag is being set.
 */
int flux_msg_set_rte (zmsg_t *zmsg, bool flag);
int flux_msg_get_rte (zmsg_t *zmsg, bool *flag);

/* Push/pop/get identity to/from routing frame stack.
 * Returned identity must be freed by caller.
 * Pushed identity is copied (caller retains ownership).
 * If message has no routing delimiter, return -1 with errno == EPROTO.
 * If routing frame stack is empty, push/get returns 0 with id set to NULL.
 */
int flux_msg_push_route (zmsg_t *zmsg, const char *id);
int flux_msg_pop_route (zmsg_t *zmsg, char **id);
int flux_msg_get_route_first (zmsg_t *zmsg, char **id); /* closest to delim */
int flux_msg_get_route_last (zmsg_t *zmsg, char **id); /* farthest from delim */
int flux_msg_get_route_count (zmsg_t *zmsg); /* -1 if no delim */

/* Get/set message type
 * For FLUX_MSGTYPE_REQUEST: set_type initializes nodeid to FLUX_NODEID_ANY
 * For FLUX_MSGTYPE_RESPONSE: set_type initializes errnum to 0
 */
int flux_msg_set_type (zmsg_t *zmsg, int type);
int flux_msg_get_type (zmsg_t *zmsg, int *type);

/* Return string representation of message type.  Do not free.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);

/**
 ** Deprecated interfaces.
 **/

char *flux_msg_nexthop (zmsg_t *zmsg);
char *flux_msg_sender (zmsg_t *zmsg);
int flux_msg_hopcount (zmsg_t *zmsg);
char *flux_msg_tag (zmsg_t *zmsg);
char *flux_msg_tag_short (zmsg_t *zmsg);
int flux_msg_decode (zmsg_t *zmsg, char **topic, json_object **o);
int flux_msg_replace_json (zmsg_t *zmsg, json_object *o);
zmsg_t *flux_msg_encode (const char *topic, json_object *o);
bool flux_msg_match (zmsg_t *msg, const char *topic);

#endif /* !_FLUX_CORE_MESSAGE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

