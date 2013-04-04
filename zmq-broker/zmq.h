typedef struct {
	zmq_msg_t tag;
	zmq_msg_t body;
} zmq_2part_t;

void _zmq_close (void *socket);
void _zmq_term (void *ctx);
void *_zmq_init (int nthreads);
void *_zmq_socket (void *ctx, int type);
void _zmq_bind (void *sock, const char *endpoint);
void _zmq_connect (void *sock, const char *endpoint);
void _zmq_subscribe (void *sock, char *tag);
void _zmq_subscribe_all (void *sock);
void _zmq_msg_init_size (zmq_msg_t *msg, size_t size);
void _zmq_msg_init (zmq_msg_t *msg);
void _zmq_msg_close (zmq_msg_t *msg);
void _zmq_send (void *socket, zmq_msg_t *msg, int flags);
void _zmq_recv (void *socket, zmq_msg_t *msg, int flags);
void _zmq_getsockopt (void *socket, int option_name, void *option_value,
                      size_t *option_len);
bool _zmq_rcvmore (void *socket);
void _zmq_msg_dup (zmq_msg_t *dest, zmq_msg_t *src);

void _zmq_2part_init (zmq_2part_t *msg);
void _zmq_2part_close (zmq_2part_t *msg);
void _zmq_2part_recv (void *socket, zmq_2part_t *msg, int flags);
void _zmq_2part_send (void *socket, zmq_2part_t *msg, int flags);
void _zmq_2part_dup (zmq_2part_t *dest, zmq_2part_t *src);
bool _zmq_2part_match (zmq_2part_t *msg, char *tag);

void _zmq_mcast_loop (void *sock, bool enable);

int _zmq_2part_recv_json (void *socket, char **tagp, json_object **op);
void _zmq_2part_send_json (void *socket, json_object *o, const char *fmt, ...);
void _zmq_2part_send_buf (void *sock, char *buf, int len, const char *fmt, ...);
