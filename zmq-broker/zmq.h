int  zpoll (zmq_pollitem_t *items, int nitems, long timeout);
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

zmsg_t *zmsg_recv_fd (int fd, int flags);
int zmsg_send_fd (int fd, zmsg_t **msg);

void zmsg_dump_compact (zmsg_t *self);
char *zmsg_route_str (zmsg_t *zmsg, int skiphops);

void zmsg_send_unrouter (zmsg_t **zmsg, void *sock, char *addr, const char *gw);
zmsg_t *zmsg_recv_unrouter (void *sock);

void zmsg_cc (zmsg_t *zmsg, void *sock);
int zmsg_hopcount (zmsg_t *zmsg);

/* cmb-specific message decoding
 */

int cmb_msg_decode (zmsg_t *msg, char **tagp, json_object **op);
zmsg_t *cmb_msg_encode (char *tag, json_object *o);

bool cmb_msg_match (zmsg_t *msg, const char *tag);
bool cmb_msg_match_substr (zmsg_t *msg, const char *tag, char **restp);

char *cmb_msg_nexthop (zmsg_t *zmsg);
char *cmb_msg_sender (zmsg_t *zmsg);
char *cmb_msg_tag (zmsg_t *zmsg, bool shorten);

int cmb_msg_rep_json (zmsg_t *zmsg, json_object *o);
int cmb_msg_rep_errnum (zmsg_t *zmsg, int errnum);


