#ifndef _FLUX_CORE_MESSAGE_H
#define _FLUX_CORE_MESSAGE_H

#include <json.h>
#include <stdbool.h>
#include <czmq.h>

/* Return the number of non-nil routing frames in the message.
 */
int flux_msg_hopcount (zmsg_t *zmsg);

/* Decode/encode cmb messages.
 * Pub/sub (event) messages will be a single tag (topic) frame.  Request/
 * response messages will have address frames, tag frame, optional json frame.
 */
int flux_msg_decode (zmsg_t *msg, char **tagp, json_object **op);
zmsg_t *flux_msg_encode (char *tag, json_object *o);

/* Match message tag frame against provided tag string.
 * flux_msg_match () requires an exact match, while
 * flux_msg_match_substr () matches on a substring and returns the rest
 * of the message's tag string in restp (if set).
 */
bool flux_msg_match (zmsg_t *msg, const char *tag);
bool flux_msg_match_substr (zmsg_t *msg, const char *tag, char **restp);

/* Get copies of message frames  Caller must free.
 */
char *flux_msg_nexthop (zmsg_t *zmsg);
char *flux_msg_sender (zmsg_t *zmsg);
char *flux_msg_tag (zmsg_t *zmsg);
char *flux_msg_tag_short (zmsg_t *zmsg);

/* Replace the json frame in a message with a new json frame,
 * or a json frame containing only an errnum value.
 */
int flux_msg_replace_json (zmsg_t *zmsg, json_object *o);
int flux_msg_replace_json_errnum (zmsg_t *zmsg, int errnum);

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

/* Return string representation of message type.
 */
const char *flux_msgtype_string (int typemask);
const char *flux_msgtype_shortstr (int typemask);


#endif /* !_FLUX_CORE_MESSAGE_H */
