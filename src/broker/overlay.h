#ifndef _BROKER_OVERLAY_H
#define _BROKER_OVERLAY_H

#include "attr.h"

typedef struct overlay_struct overlay_t;
typedef void (*overlay_cb_f)(overlay_t *ov, void *sock, void *arg);

overlay_t *overlay_create (void);
void overlay_destroy (overlay_t *ov);

/* These need to be called before connect/bind.
 */
void overlay_set_sec (overlay_t *ov, flux_sec_t *sec);
void overlay_set_flux (overlay_t *ov, flux_t *h);
void overlay_init (overlay_t *ov, uint32_t size, uint32_t rank, int tbon_k);
void overlay_set_idle_warning (overlay_t *ov, int heartbeats);

/* Accessors
 */
uint32_t overlay_get_rank (overlay_t *ov);
uint32_t overlay_get_size (overlay_t *ov);

/* All ranks but rank 0 connect to a parent to form the main TBON.
 */
void overlay_set_parent (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_parent (overlay_t *ov);
void overlay_set_parent_cb (overlay_t *ov, overlay_cb_f cb, void *arg);
int overlay_sendmsg_parent (overlay_t *ov, const flux_msg_t *msg);

/* The child is where other ranks connect to send requests.
 * This is the ROUTER side of parent sockets described above.
 */
void overlay_set_child (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_child (overlay_t *ov);
void overlay_set_child_cb (overlay_t *ov, overlay_cb_f cb, void *arg);
int overlay_sendmsg_child (overlay_t *ov, const flux_msg_t *msg);
/* We can "multicast" events to all child peers using mcast_child().
 * It walks the 'children' hash, finding overlay peers that have not
 * yet been "muted", and routes them a copy of msg.  The broker Cc's
 * events over the TBON using this until peers indicate that they are
 * receiving duplicate seq numbers through the normal event socket.
 */
int overlay_mcast_child (overlay_t *ov, const flux_msg_t *msg);
void overlay_mute_child (overlay_t *ov, const char *uuid);

/* Call when message is received from child 'uuid'.
 */
void overlay_checkin_child (overlay_t *ov, const char *uuid);

/* Encode cmb.lspeer response payload.
 */
char *overlay_lspeer_encode (overlay_t *ov);

/* The event socket is SUB for ranks > 0, and PUB for rank 0.
 * Internally, all events are routed to rank 0 before being published.
 */
void overlay_set_event (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_event (overlay_t *ov);
void overlay_set_event_cb (overlay_t *ov, overlay_cb_f cb, void *arg);
int overlay_sendmsg_event (overlay_t *ov, const flux_msg_t *msg);
flux_msg_t *overlay_recvmsg_event (overlay_t *ov);

/* Since an epgm:// endpoint only allows one subscriber per node,
 * when there are multiple ranks per node, arrangements must be made
 * to forward events within a clique.  Only the relay itself has this
 * socket; other clique members would subscribe to the relay's URI
 * via their main event socket.  The PMI bootstrap sets this up if needed.
 */
void overlay_set_relay (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_relay (overlay_t *ov);
int overlay_sendmsg_relay (overlay_t *ov, const flux_msg_t *msg);

/* Establish connections.
 * These functions are idempotent as the bind may need to be called
 * early to resolve wildcard addresses (e.g. during PMI endpoint exchange).
 */
int overlay_bind (overlay_t *ov);
int overlay_connect (overlay_t *ov);

/* Switch parent DEALER socket to a new peer.  If the uri is already present
 * in the parent endpoint stack, reuse the existing socket ('recycled' set
 * to true).  The new parent is moved to the top of the parent stack.
 */
int overlay_reparent (overlay_t *ov, const char *uri, bool *recycled);

/* Add attributes to 'attrs' to reveal information about the overlay
 * network.  Two of the attributes directly retrieve information from
 * "overlay" through callbacks registered with 'attrs': "tbon.parent-endpoint"
 * and "mcast.relay-endpoint".  The rest are simple non-active attributes:
 * "rank", "size", "tbon.arity", "tbon.level", "tbon.maxlevel", and
 * "tbon.descendants".
 *
 * Returns 0 on success, -1 on error.
 */
int overlay_register_attrs (overlay_t *overlay, attr_t *attrs);


#endif /* !_BROKER_OVERLAY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
