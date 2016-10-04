#ifndef _BROKER_SNOOP_H
#define _BROKER_SNOOP_H

/* Manage the broker "snoop socket" which is created on demand.
 * Call snoop_create(), then
 * snoop_set_sec()   - (optional) if security is enabled
 * snoop_set_zctx()  - broker zctx_t needed for socket creation
 * snoop_set_uri()   - socket URI or wildcard

 * Until snoop_get_uri() is called, the PUB socket is not created and
 * calls to snoop_sendmsg() are a no-op.

 * When flux-snoop(1) requests the snoop socket URI from the broker,
 * the broker calls snoop_get_uri() which binds the socket on the first call.
 * Thereafter, snoop_sendmsg() publishes messages on the snoop socket.
 *
 * If it is a wildcard, snoop_get_uri() will return the
 * actual URI after the socket has been bound, not the wildcard.
 *
 * If the snoop socket is an ipc:// socket, it is cleaned up through an
 * exit handler.
 */

typedef struct snoop_struct snoop_t;

snoop_t *snoop_create (void);
void snoop_destroy (snoop_t *sn);

void snoop_set_sec (snoop_t *sn, flux_sec_t *sec);
void snoop_set_zctx (snoop_t *sn, zctx_t *zctx);
void snoop_set_uri (snoop_t *sn, const char *fmt, ...);

const char *snoop_get_uri (snoop_t *sn);
int snoop_sendmsg (snoop_t *sn, const flux_msg_t *msg);

#endif /* !_BROKER_SNOOP_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
