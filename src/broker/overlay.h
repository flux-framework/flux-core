#ifndef _BROKER_OVERLAY_H
#define _BROKER_OVERLAY_H

typedef struct {
    zctx_t *zctx;
    flux_sec_t sec;
    zloop_t *zloop;

    uint32_t rank;
    char rankstr[16];
    char rankstr_right[16];

    zlist_t *parents;           /* DEALER - requests to parent */
                                /*  (reparent pushes new parent on head) */
    endpt_t *right;             /* DEALER - requests to rank overlay */
    zloop_fn *parent_cb;
    void *parent_arg;

    endpt_t *child;             /* ROUTER - requests from children */
    zloop_fn *child_cb;
    void *child_arg;

    endpt_t *event;             /* PUB for rank = 0, SUB for rank > 0 */
    endpt_t *relay;
    zloop_fn *event_cb;
    void *event_arg;

} overlay_t;

overlay_t *overlay_create (void);
void overlay_destroy (overlay_t *ov);

/* These need to be set before connect/bind.
 */
void overlay_set_sec (overlay_t *ov, flux_sec_t sec);
void overlay_set_zctx (overlay_t *ov, zctx_t *zctx);
void overlay_set_rank (overlay_t *ov, uint32_t rank);
void overlay_set_zloop (overlay_t *ov, zloop_t *zloop);

/* All ranks but rank 0 connect to a parent to form the main TBON.
 * Internally there is a stack of parent URI's, with top as primary.
 * When we reparent (e.g. for failover), a new current parent is selected
 * and moved to the top.  Old parent sockets are not closed; they may
 * still trigger the parent callback, but only the primary is used for sends.
 *
 * The 'right' socket is an alternate topology (ring) used for rank-
 * addressed requests other than to self or rank 0.  It connects to
 * rank - 1, wrapped.
 *
 * These are DEALER sockets and conform to the ZeroMQ DEALER-ROUTER
 * message flow.
 */
void overlay_push_parent (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_parent (overlay_t *ov);
void overlay_set_right (overlay_t *ov, const char *fmt, ...);
void overlay_set_parent_cb (overlay_t *ov, zloop_fn *cb, void *arg);

/* The child is where other ranks connect to send requests.
 * This is the ROUTER side of parent/right sockets described above.
 */
void overlay_set_child (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_child (overlay_t *ov);
void overlay_set_child_cb (overlay_t *ov, zloop_fn *cb, void *arg);

/* The event socket is SUB for ranks > 0, and PUB for rank 0.
 * Internally, all events are routed to rank 0 before being published.
 */
void overlay_set_event (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_event (overlay_t *ov);
void overlay_set_event_cb (overlay_t *ov, zloop_fn *cb, void *arg);

/* Since an epgm:// endpoint only allows one subscriber per node,
 * when there are multiple ranks per node, arrangements must be made
 * to forward events within a clique.  Only the relay itself has this
 * socket; other clique members would subscribe to the relay's URI
 * via their main event socket.  The PMI bootstrap sets this up if needed.
 */
void overlay_set_relay (overlay_t *ov, const char *fmt, ...);
const char *overlay_get_relay (overlay_t *ov);

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

/* Accessors for sockets created by bind/connect.
 */
void *overlay_get_parent_sock (overlay_t *ov);
void *overlay_get_right_sock (overlay_t *ov);
void *overlay_get_child_sock (overlay_t *ov);
void *overlay_get_event_sock (overlay_t *ov);
void *overlay_get_relay_sock (overlay_t *ov);

#endif /* !_BROKER_OVERLAY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
