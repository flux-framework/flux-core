#ifndef _HAVE_ZMSG_H
#define _HAVE_ZMSG_H

/* Create socket, set hwm, set identity, connect/bind all in one go.
 * All errors are fatal.
 */
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

/* Send/recv zmq messages over a file descriptor.
 * Utilizes zmsg_encode/zmsgs_decode() functions from czmq.
 * N.B. The nonblock flag doesn't completely eliminate blocking.
 * Once a message has begun to be read, the recv may block in order
 * to read the complete thing.
 */
zmsg_t *zmsg_recv_fd (int fd, bool nonblock);
int zmsg_send_fd (int fd, zmsg_t **msg);

zmsg_t *zmsg_recv_fd_typemask (int fd, int *typemask, bool nonblock);
int zmsg_send_fd_typemask (int fd, int typemask, zmsg_t **msg);

/* Format message frames as text.  The first prints entire message on stdout.
 * The second returns a string representing only routing frames that the
 * caller must free.
 */
void zmsg_dump_compact (zmsg_t *self, const char *prefix);
char *zmsg_route_str (zmsg_t *zmsg, int skiphops);

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
