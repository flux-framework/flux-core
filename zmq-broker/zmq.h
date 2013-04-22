int  zpoll (zmq_pollitem_t *items, int nitems, long timeout);
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

/* Like zmsg_recv/zmsg_send but for a SOCK_SEQPACKET socket */
zmsg_t *zmsg_recv_fd (int fd, int flags);
int zmsg_send_fd (int fd, zmsg_t **msg);

/* CMB "protocol" based on multipart messages (zmsg_t):
 * always a tag part;
 * then, sometimes a JSON part;
 * then (only if there is a JSON part), sometimes an opaque part.
 */
int cmb_msg_decode (zmsg_t *msg, char **tagp, json_object **op,
                    void **datap, int *lenp);
zmsg_t *cmb_msg_encode (char *tag, json_object *o, void *data, int len);

void cmb_msg_send_long (void *sock, json_object *o, void *data, int len,
                        const char *fmt, ...)
        		__attribute__ ((format (printf, 5, 6)));

void cmb_msg_send (void *sock, json_object *o, const char *fmt, ...)
        	   __attribute__ ((format (printf, 3, 4)));

int cmb_msg_recv (void *socket, char **tagp, json_object **op,
                  void **datap, int *lenp, int flags);

int cmb_msg_send_long_fd (int fd, json_object *o, void *data, int len,
                          const char *fmt, ...)
        	          __attribute__ ((format (printf, 5, 6)));

int cmb_msg_send_fd (int fd, json_object *o, const char *fmt, ...)
        	     __attribute__ ((format (printf, 3, 4)));

int cmb_msg_recv_fd (int fd, char **tagp, json_object **op,
                     void **datap, int *lenp, int flags);

bool cmb_msg_match (zmsg_t *msg, const char *tag);
bool cmb_msg_match_substr (zmsg_t *msg, const char *tag, char **restp);

char *cmb_msg_sender (zmsg_t *zmsg);

int cmb_msg_rep_nak (zmsg_t *zmsg);
int cmb_msg_rep_json (zmsg_t *zmsg, json_object *o);

int cmb_msg_datacpy (zmsg_t *msg, char *buf, int len);

