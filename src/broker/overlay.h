/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef BROKER_OVERLAY_H
#define BROKER_OVERLAY_H

#include <jansson.h>
#include <stdint.h>

#include "src/common/libpmi/bizcard.h"
#include "src/common/libpmi/upmi.h"

#include "attr.h"
#include "topology.h"
#include "bootstrap.h"

/* Overlay control messages
 */
enum control_type {
    CONTROL_HEARTBEAT = 0, // child sends when connection is idle
    CONTROL_STATUS = 1,    // child tells parent of subtree status change
    CONTROL_DISCONNECT = 2,// parent tells child to immediately disconnect
};

struct overlay;

/* Create overlay network, registering 'cb' to be called with each
 * received message.
 * Note: If zctx is NULL, it is created/destroyed on demand internally.
 */
struct overlay *overlay_create (flux_t *h,
                                struct bootstrap *boot,
                                struct upmi_info *info,
                                const char *hostname,
                                attr_t *attrs,
                                void *zctx,
                                const char *uri,
                                flux_error_t *error);
void overlay_destroy (struct overlay *ov);

/* Start sending control messages to parent and monitoring peers.
 * This registers a sync callback, and will fail if event.subscribe
 * doesn't have a handler yet.
 */
int overlay_start (struct overlay *ov);

/* Set the overlay topology.
 */
int overlay_set_topology (struct overlay *ov, struct topology *topo);

/* Each broker has a public, private CURVE key-pair.  Call overlay_authorize()
 * with the public key of each downstream peer to authorize it to connect,
 * and overlay_set_parent_pubkey() with the public key of the parent
 * before calling overlay_connect().
 * NOTE: if bootstrapping with PMI, unique public keys are generated for
 * each broker and shared via PMI exchange.  If bootstrapping with config
 * files, each broker loads an (assumed) identical key-pair from a file.
 * Only the public key may be shared over the network, never the private key.
 */
int overlay_cert_load (struct overlay *ov,
                       const char *path,
                       flux_error_t *error);
const char *overlay_cert_pubkey (struct overlay *ov);
const char *overlay_cert_name (struct overlay *ov);
int overlay_authorize (struct overlay *ov,
                       const char *name,
                       const char *pubkey);
int overlay_set_parent_pubkey (struct overlay *ov, const char *pubkey);

/* Misc. accessors
 */
const struct bizcard *overlay_get_bizcard (struct overlay *ov);
int overlay_set_parent_uri (struct overlay *ov, const char *uri);
void overlay_set_ipv6 (struct overlay *ov, int enable);
int overlay_set_tbon_interface_hint (struct overlay *ov, const char *val);

/* Return an idset of critical ranks, i.e. non-leaf brokers
 */
struct idset *overlay_get_default_critical_ranks (struct overlay *ov);

/* Broker should call overlay_bind() if there are children.  This may happen
 * before any peers are authorized as long as they are authorized before they
 * try to connect.  Note: uri2 (a secondary endpoint) MAY be NULL
 */
int overlay_bind (struct overlay *ov,
                  const char *uri,
                  const char *uri2,
                  flux_error_t *error);

/* Broker should call overlay_connect(), after overlay_set_parent_uri()
 * and overlay_set_parent_pubkey(), if there is a parent.
 */
int overlay_connect (struct overlay *ov);

/* Private to overlay unit test
 */
void overlay_test_set_rank (struct overlay *ov, uint32_t rank);
void overlay_test_set_version (struct overlay *ov, int version);

#endif /* !BROKER_OVERLAY_H */
/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
