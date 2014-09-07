#ifndef _UTIL_ZFD_H
#define _UTIL_ZFD_H

/* Send/recv zmq messages over a file descriptor.
 * Utilizes zmsg_encode/zmsg_decode() functions from czmq.
 * N.B. The nonblock flag doesn't completely eliminate blocking.
 * Once a message has begun to be read, the recv may block in order
 * to read the complete thing.
 */
zmsg_t *zfd_recv (int fd, bool nonblock);
int zfd_send (int fd, zmsg_t **msg);

zmsg_t *zfd_recv_typemask (int fd, int *typemask, bool nonblock);
int zfd_send_typemask (int fd, int typemask, zmsg_t **msg);

#endif /* !_UTIL_ZFD_H */
