#ifndef _UTIL_ZMSG_H
#define _UTIL_ZMSG_H

/* Return the number of non-nil routing frames in the message.
 */
int zmsg_hopcount (zmsg_t *zmsg);

/* Decode/encode cmb messages.
 * Pub/sub (event) messages will be a single tag (topic) frame.  Request/
 * response messages will have address frames, tag frame, optional json frame.
 */
int cmb_msg_decode (zmsg_t *msg, char **tagp, json_object **op);
zmsg_t *cmb_msg_encode (char *tag, json_object *o);

/* Match message tag frame against provided tag string.
 * cmb_msg_match () requires an exact match, while
 * cmb_msg_match_substr () matches on a substring and returns the rest
 * of the message's tag string in restp (if set).
 */
bool cmb_msg_match (zmsg_t *msg, const char *tag);
bool cmb_msg_match_substr (zmsg_t *msg, const char *tag, char **restp);

/* Get copies of message frames  Caller must free.
 */
char *cmb_msg_nexthop (zmsg_t *zmsg);
char *cmb_msg_sender (zmsg_t *zmsg);
char *cmb_msg_tag (zmsg_t *zmsg, bool shorten);

/* Replace the json frame in a message with a new json frame,
 * or a json frame containing only an errnum value.
 */
int cmb_msg_replace_json (zmsg_t *zmsg, json_object *o);
int cmb_msg_replace_json_errnum (zmsg_t *zmsg, int errnum);


#endif /* !_HAVE_ZMSG_H */
