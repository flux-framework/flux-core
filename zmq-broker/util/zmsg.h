#ifndef _HAVE_ZMSG_H
#define _HAVE_ZMSG_H

/* Create socket, set hwm, set identity, connect/bind all in one go.
 * All errors are fatal.
 */
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

/* Helpers for zmq_socket_monitor.
 */
void *zmonitor (zctx_t *ctx, void *s, const char *uri, int flags);
void zmonitor_recv (void *s, zmq_event_t *event, bool *valid_addrsp);
void zmonitor_dump (char *name, void *s);

/* Send/recv zmq messages over a file descriptor.
 * Utilizes zmsg_encode/zmsgs_decode() functions from czmq.
 * N.B. The nonblock flag doesn't completely eliminate blocking.
 * Once a message has begun to be read, the recv may block in order
 * to read the complete thing.
 */
zmsg_t *zmsg_recv_fd (int fd, bool nonblock);
int zmsg_send_fd (int fd, zmsg_t **msg);

/* Format message frames as text.  The first prints entire message on stdout.
 * The second returns a string representing only routing frames that the
 * caller must free.
 */
void zmsg_dump_compact (zmsg_t *self);
char *zmsg_route_str (zmsg_t *zmsg, int skiphops);

/* For "reverse" message flow over dealer-router:
 *   zmsg_send_unrouter() - pushes local adress for reply path, then gw addr
 *                          for routing socket.
 *   zmsg_recv_unrouter() - pops two frames and destroys them.
 * Use both on router socket.  Dealer socket requires no intervention.
 */
void zmsg_send_unrouter (zmsg_t **zmsg, void *sock, char *addr, const char *gw);
zmsg_t *zmsg_recv_unrouter (void *sock);

/* Send a copy of zmsg to sock.
 */
void zmsg_cc (zmsg_t *zmsg, void *sock);

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
