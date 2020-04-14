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

struct overlay;

typedef void (*overlay_cb_f)(struct overlay *ov, void *sock, void *arg);
typedef int (*overlay_init_cb_f)(struct overlay *ov, void *arg);
typedef void (*overlay_monitor_cb_f)(struct overlay *ov, void *arg);

struct overlay *overlay_create (flux_t *h,
                                int sec_typemask,
                                const char *keydir);
void overlay_destroy (struct overlay *ov);

/* Set a callback triggered during overlay_init()
 */
void overlay_set_init_callback (struct overlay *ov,
                                overlay_init_cb_f cb,
                                void *arg);

/* These need to be called before connect/bind.
 */
int overlay_init (struct overlay *ov,
                  uint32_t size,
                  uint32_t rank,
                  int tbon_k);
void overlay_set_idle_warning (struct overlay *ov, int heartbeats);

/* Accessors
 */
uint32_t overlay_get_rank (struct overlay *ov);
uint32_t overlay_get_size (struct overlay *ov);
int overlay_get_child_peer_count (struct overlay *ov);

/* All ranks but rank 0 connect to a parent to form the main TBON.
 */
int overlay_set_parent (struct overlay *ov, const char *fmt, ...);
const char *overlay_get_parent (struct overlay *ov);
void overlay_set_parent_cb (struct overlay *ov, overlay_cb_f cb, void *arg);
int overlay_sendmsg_parent (struct overlay *ov, const flux_msg_t *msg);

/* The child is where other ranks connect to send requests.
 * This is the ROUTER side of parent sockets described above.
 */
int overlay_set_child (struct overlay *ov, const char *fmt, ...);
const char *overlay_get_child (struct overlay *ov);
void overlay_set_child_cb (struct overlay *ov, overlay_cb_f cb, void *arg);
int overlay_sendmsg_child (struct overlay *ov, const flux_msg_t *msg);
/* We can "multicast" events to all child peers using mcast_child().
 * It walks the 'children' hash, finding peers and routeing them a copy of msg.
 */
int overlay_mcast_child (struct overlay *ov, const flux_msg_t *msg);

/* Call when message is received from child 'uuid'.
 */
void overlay_checkin_child (struct overlay *ov, const char *uuid);

/* Register callback that will be called each time a child connects/disconnects.
 * Use overlay_get_child_peer_count() to access the actual count.
 */
void overlay_set_monitor_cb (struct overlay *ov,
                             overlay_monitor_cb_f cb,
                             void *arg);

/* Encode cmb.lspeer response payload.
 */
char *overlay_lspeer_encode (struct overlay *ov);

/* Establish connections.
 * These functions are idempotent as the bind may need to be called
 * early to resolve wildcard addresses (e.g. during PMI endpoint exchange).
 */
int overlay_bind (struct overlay *ov);
int overlay_connect (struct overlay *ov);

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
int overlay_register_attrs (struct overlay *overlay, attr_t *attrs);

#endif /* !_BROKER_OVERLAY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
