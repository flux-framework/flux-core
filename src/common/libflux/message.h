#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>
#include <stdint.h>

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

/* Get/set nodeid (request only)
 */
#define FLUX_NODEID_ANY	(~(uint32_t)0)
int flux_msg_set_nodeid (zmsg_t *zmsg, uint32_t nodeid);
int flux_msg_get_nodeid (zmsg_t *zmsg, uint32_t *nodeid);

/* Get/set errnum (response only)
 */
int flux_msg_set_errnum (zmsg_t *zmsg, int errnum);
int flux_msg_get_errnum (zmsg_t *zmsg, int *errnum);

/* Message manipulation utility functions
 */
enum {
    FLUX_MSGTYPE_REQUEST = 1,
    FLUX_MSGTYPE_RESPONSE = 2,
    FLUX_MSGTYPE_EVENT = 4,
    FLUX_MSGTYPE_ANY = 7,
    FLUX_MSGTYPE_MASK = 7,
    /* leave open possiblity of adding 'flags' bits here */
};

/* Return string representation of message type.  Do not free.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);


#endif /* !_FLUX_CORE_MESSAGE_H */
