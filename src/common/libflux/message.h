#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>
#include <stdint.h>

/* Create a new Flux message of the specified type.
 * If non-null, set topic.
 * If non-null and non-zero len, set payload.
 * Returns new message or null on failure, with errno set (e.g. ENOMEM)
 * Caller must destroy message with zmsg_destroy() or equivalent.
 */
zmsg_t *flux_msg_create (int type, const char *topic, void *buf, int len);

/* Return the number of non-nil routing frames in a Flux message.
 */
int flux_msg_hopcount (zmsg_t *zmsg);

/* Get topic string and JSON payload from a Flux message.
 * Caller must free () topic, and json_object_put () payload.
 */
int flux_msg_decode (zmsg_t *msg, char **topic, json_object **payload);

/* Create a new Flux message containing topic and payload.
 * Copies are made internally of topic and payload.  Payload may be NULL.
 */
zmsg_t *flux_msg_encode (char *topic, json_object *payload);

/* Check whether message's topic string matches the provided one (verbatim).
 */
bool flux_msg_match (zmsg_t *msg, const char *topic);

/* Get identity from routing frame (if any) furthest from payload.
 * When sending a response, this is the next hop.  However, when receiving
 * a request, it is the last hop.  Caller must free.
 */
char *flux_msg_nexthop (zmsg_t *zmsg);

/* Get sender identity from routing frame (if any) closest to payload.
 * Caller must free.
 */
char *flux_msg_sender (zmsg_t *zmsg);

/* Get copy of message topic string.  Caller must free.
 */
char *flux_msg_tag (zmsg_t *zmsg);

/* Get copy of message topic string, shortened to the first "word", where
 * words are separated by '.' characters.  Caller must free.
 */
char *flux_msg_tag_short (zmsg_t *zmsg);

/* Replace the json frame in a message with a new json frame 'o'.
 * If 'o' is NULL, delete the message's JSON frame, if any.
 * If 'o' is non-NULL and the message lacks a JSON frame, add one.
 */
int flux_msg_replace_json (zmsg_t *zmsg, json_object *o);

/* Get/set message type
 * For FLUX_MSGTYPE_REQUEST: set_type initializes nodeid to FLUX_NODEID_ANY
 * For FLUX_MSGTYPE_RESPONSE: set_type initializes errnum to 0
 */
int flux_msg_set_type (zmsg_t *zmsg, int type);
int flux_msg_get_type (zmsg_t *zmsg, int *type);

/* Get/set message flags
 */
int flux_msg_set_flags (zmsg_t *zmsg, uint8_t flags);
int flux_msg_get_flags (zmsg_t *zmsg, uint8_t *flags);

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

/* Message manipulation utility functions
 */
enum {
    FLUX_MSGTYPE_REQUEST    = 0x01,
    FLUX_MSGTYPE_RESPONSE   = 0x02,
    FLUX_MSGTYPE_EVENT      = 0x04,
    FLUX_MSGTYPE_KEEPALIVE  = 0x08,
    FLUX_MSGTYPE_ANY        = 0x0f,
    FLUX_MSGTYPE_MASK       = 0x0f,
};

enum {
    FLUX_MSGFLAG_TOPIC      = 0x01,
    FLUX_MSGFLAG_PAYLOAD    = 0x02,
    FLUX_MSGFLAG_JSON       = 0x04,
};

/* Return string representation of message type.  Do not free.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);


#endif /* !_FLUX_CORE_MESSAGE_H */
