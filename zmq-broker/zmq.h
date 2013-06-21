int  zpoll (zmq_pollitem_t *items, int nitems, long timeout);
void zconnect (zctx_t *zctx, void **sp, int type, char *uri, int hwm, char *id);
void zbind (zctx_t *zctx, void **sp, int type, char *uri, int hwm);

zmsg_t *zmsg_recv_fd (int fd, int flags);
int zmsg_send_fd (int fd, zmsg_t **msg);

void zmsg_send_unrouter (zmsg_t **zmsg, void *sock, char *addr, char *gw);
zmsg_t *zmsg_recv_unrouter (void *sock);

void zmsg_cc (zmsg_t *zmsg, void *sock);

int cmb_msg_decode (zmsg_t *msg, char **tagp, json_object **op,
                    void **datap, int *lenp);
zmsg_t *cmb_msg_encode (char *tag, json_object *o, void *data, int len);

void cmb_msg_send_long (void *sock, json_object *o, void *data, int len,
                        const char *fmt, ...)
        		__attribute__ ((format (printf, 5, 6)));

void cmb_msg_send (void *sock, json_object *o, const char *fmt, ...)
        	   __attribute__ ((format (printf, 3, 4)));
void cmb_msg_send_rt (void *sock, json_object *o, const char *fmt, ...)
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
bool cmb_msg_match_sender (zmsg_t *zmsg, const char *sender);

char *cmb_msg_sender (zmsg_t *zmsg);
char *cmb_msg_tag (zmsg_t *zmsg, bool shorten);
int cmb_msg_hopcount (zmsg_t *zmsg);

int cmb_msg_rep_json (zmsg_t *zmsg, json_object *o);
int cmb_msg_rep_errnum (zmsg_t *zmsg, int errnum);
void cmb_msg_send_errnum (zmsg_t **zmsg, void *socket, int errnum, void *cc);

int cmb_msg_datacpy (zmsg_t *msg, char *buf, int len);
void cmb_dump (zmsg_t *self);

