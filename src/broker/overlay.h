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

#include <jansson.h>
#include <stdint.h>

#include "attr.h"

typedef enum {
    OVERLAY_ANY = 0,
    OVERLAY_UPSTREAM,
    OVERLAY_DOWNSTREAM,
} overlay_where_t;

struct overlay;

typedef void (*overlay_monitor_f)(struct overlay *ov, void *arg);
typedef void (*overlay_recv_f)(const flux_msg_t *msg,
                               overlay_where_t from,
                               void *arg);
typedef void (*overlay_loss_f)(struct overlay *ov,
                               uint32_t rank,
                               const char *status,
                               json_t *topo,
                               void *arg);

/* Create overlay network, registering 'cb' to be called with each
 * received message.
 */
struct overlay *overlay_create (flux_t *h,
                                attr_t *attrs,
                                overlay_recv_f cb,
                                void *arg);
void overlay_destroy (struct overlay *ov);

/* Start sending keepalive messages to parent and monitoring peers.
 * This registers a sync callback, and will fail if event.subscribe
 * doesn't have a handler yet.
 */
int overlay_keepalive_start (struct overlay *ov);

/* Set the overlay network size and rank of this broker.
 */
int overlay_set_geometry (struct overlay *ov, uint32_t size, uint32_t rank);

/* Send a message on the overlay network.
 * 'where' determines whether the message is routed upstream or downstream.
 */
int overlay_sendmsg (struct overlay *ov,
                     const flux_msg_t *msg,
                     overlay_where_t where);

/* Each broker has a public, private CURVE key-pair.  Call overlay_authorize()
 * with the public key of each downstream peer to authorize it to connect,
 * and overlay_set_parent_pubkey() with the public key of the parent
 * before calling overlay_connect().
 * NOTE: if bootstrapping with PMI, unique public keys are generated for
 * each broker and shared via PMI exchange.  If boostrapping with config
 * files, each broker loads an (assumed) identical key-pair from a file.
 * Only the public key may be shared over the network, never the private key.
 */
int overlay_cert_load (struct overlay *ov, const char *path);
const char *overlay_cert_pubkey (struct overlay *ov);
const char *overlay_cert_name (struct overlay *ov);
int overlay_authorize (struct overlay *ov,
                       const char *name,
                       const char *pubkey);
int overlay_set_parent_pubkey (struct overlay *ov, const char *pubkey);

/* Misc. accessors
 */
int overlay_get_fanout (struct overlay *ov);
uint32_t overlay_get_rank (struct overlay *ov);
void overlay_set_rank (struct overlay *ov, uint32_t rank); // test only
uint32_t overlay_get_size (struct overlay *ov);
int overlay_get_child_peer_count (struct overlay *ov);
const char *overlay_get_bind_uri (struct overlay *ov);
const char *overlay_get_parent_uri (struct overlay *ov);
int overlay_set_parent_uri (struct overlay *ov, const char *uri);
bool overlay_parent_error (struct overlay *ov);
void overlay_set_version (struct overlay *ov, int version); // test only
const char *overlay_get_uuid (struct overlay *ov);
bool overlay_uuid_is_parent (struct overlay *ov, const char *uuid);
bool overlay_uuid_is_child (struct overlay *ov, const char *uuid);
void overlay_set_ipv6 (struct overlay *ov, int enable);

/* Fetch TBON subtree topo at 'rank'.  The returned topology object has the
 * following recursive structure, where "children" is an array of topology
 * objects:
 *
 * {"rank":i, "size":i, "children":o}
 *
 * If rank has no children, the "children" array will be present but empty.
 * Caller must release returned object with json_decref().
 */
json_t *overlay_get_subtree_topo (struct overlay *ov, int rank);

/* Fetch status for TBON subtree rooted at 'rank'.  If 'rank' is not this
 * broker's rank or one of its direct descendants, "unknown" is returned.
 */
const char *overlay_get_subtree_status (struct overlay *ov, int rank);

/* Broker should call overlay_bind() if there are children.  This may happen
 * before any peers are authorized as long as they are authorized before they
 * try to connect.
 */
int overlay_bind (struct overlay *ov, const char *uri);

/* Broker should call overlay_connect(), after overlay_set_parent_uri()
 * and overlay_set_parent_pubkey(), if there is a parent.
 */
int overlay_connect (struct overlay *ov);

/* 'cb' is called each time the number of connected TBON peers changes,
 * or when a TBON parent error occurs.  Use overlay_get_child_peer_count(),
 * overlay_parent_error() from the callback.
 */
void overlay_set_monitor_cb (struct overlay *ov,
                             overlay_monitor_f cb,
                             void *arg);

void overlay_set_loss_cb (struct overlay *ov,
                          overlay_loss_f cb,
                          void *arg);

/* Register overlay-related broker attributes.
 */
int overlay_register_attrs (struct overlay *overlay);

#endif /* !_BROKER_OVERLAY_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
