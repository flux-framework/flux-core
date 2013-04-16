int  _zmq_poll (zmq_pollitem_t *items, int nitems, long timeout);

zmsg_t *zmsg_recv_fd (int fd, int flags);
int zmsg_send_fd (int fd, zmsg_t **msg);

void cmb_msg_send_long (void *sock, json_object *o, void *data, int len,
                        const char *fmt, ...);
void cmb_msg_send (void *sock, const char *fmt, ...);
int cmb_msg_recv (void *socket, char **tagp, json_object **op,
                  void **datap, int *lenp, int flags);

int cmb_msg_send_long_fd (int fd, json_object *o, void *data, int len,
                           const char *fmt, ...);

int cmb_msg_send_fd (int fd, const char *fmt, ...);
int cmb_msg_recv_fd (int fd, char **tagp, json_object **op,
                     void **datap, int *lenp, int flags);

bool cmb_msg_match (zmsg_t *msg, const char *tag, bool exact);

int cmb_msg_datacpy (zmsg_t *msg, char *buf, int len);
