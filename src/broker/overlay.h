/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_OVERLAY_H
#define _BROKER_OVERLAY_H

#include "attr.h"
#include "src/common/libutil/zsecurity.h"

typedef struct overlay_struct overlay_t;
typedef void (*overlay_cb_f)(overlay_t *ov, void *sock, void *arg);
typedef void (*overlay_init_cb_f)(overlay_t *ov, void *arg);

overlay_t *overlay_create (void);
void overlay_destroy (overlay_t *ov);

/* Set a callback triggered during overlay_init()
 */
void overlay_set_init_callback (overlay_t *ov,
                                overlay_init_cb_f cb, void *arg);

/* These need to be called before connect/bind.
 */
int overlay_set_flux (overlay_t *ov, flux_t *h);
int overlay_setup_sec (overlay_t *ov, int sec_typemask, const char *keydir);
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
 * It walks the 'children' hash, finding peers and routeing them a copy of msg.
 */
int overlay_mcast_child (overlay_t *ov, const flux_msg_t *msg);

/* Call when message is received from child 'uuid'.
 */
void overlay_checkin_child (overlay_t *ov, const char *uuid);

/* Encode cmb.lspeer response payload.
 */
char *overlay_lspeer_encode (overlay_t *ov);

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

/* Add attributes to 'attrs' to reveal information about the overlay network.
 * Active attrs:
 *   tbon.parent-endpoint
 * Passive attrs:
 *   rank
 *   size
 *   tbon.arity
 *   tbon.level
 *   tbon.maxlevel
 *   tbon.descendants
 * Returns 0 on success, -1 on error.
 */
int overlay_register_attrs (overlay_t *overlay, attr_t *attrs);


#endif /* !_BROKER_OVERLAY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
