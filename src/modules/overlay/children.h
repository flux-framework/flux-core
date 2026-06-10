/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_OVERLAY_CHILDREN_H
#define _FLUX_OVERLAY_CHILDREN_H

#include <flux/core.h>
#include "src/common/librouter/rpc_track.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libzmqutil/cert.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libzmqutil/zap.h"
#include "topology.h"
#include "ovconf.h"
#include "ccan/str/str.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37
#endif

/* Numerical values for "subtree health" so we can send them in control
 * messages.  Textual values below will be used for communication with front
 * end diagnostic tool.
 */
enum subtree_status {
    SUBTREE_STATUS_UNKNOWN = 0,
    SUBTREE_STATUS_FULL = 1,
    SUBTREE_STATUS_PARTIAL = 2,
    SUBTREE_STATUS_DEGRADED = 3,
    SUBTREE_STATUS_LOST = 4,
    SUBTREE_STATUS_OFFLINE = 5,
    SUBTREE_STATUS_MAXIMUM = 5,
};

struct child {
    double lastseen;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    enum subtree_status status;
    struct timespec status_timestamp;
    bool torpid;
    struct rpc_track *tracker;
    flux_error_t error;
};

/* Container for child peer state */
struct children {
    struct child *children;
    int count;
    zhashx_t *hash;         // online children only, keyed by uuid
    struct topology *topo;  // borrowed reference, provides O(1) lookup by rank
    flux_t *h;              // borrowed reference for logging
    void *bind_zsock;       // ROUTER socket for children connections
    flux_watcher_t *bind_w; // watcher for bind socket
    struct zmqutil_monitor *bind_monitor;
    struct zmqutil_zap *zap; // ZAP authentication server
};

/* Convenience iterator for children array.
 * NULL-safe: if ctx is NULL, the loop body is skipped.
 */
#define children_foreach(ctx, child) \
    for ((child) = (ctx) ? &(ctx)->children[0] : NULL; \
         (child) && ((child) - &(ctx)->children[0] < (ctx)->count); \
         (child)++)

/* Create and populate children container from topology.
 * Returns pointer on success, NULL on error with errno set.
 */
struct children *children_create (flux_t *h, struct topology *topo);

/* Destroy children container.
 */
void children_destroy (struct children *ctx);

/* Lookup direct child peer by UUID, by scanning array.
 * Returns NULL if not found or id is NULL.
 */
struct child *children_lookup (struct children *ctx, const char *id);

/* Look up direct child peer by UUID using the hash table.
 * N.B. Only online children are in the hash.
 * Returns NULL if not found.
 */
struct child *children_lookup_online (struct children *ctx, const char *id);

/* Lookup direct child peer by rank using fast topology map.
 * Returns NULL if not found.
 */
struct child *children_lookup_byrank (struct children *ctx, uint32_t rank);

/* Look up child that provides route to 'rank' using topology.
 * Returns NULL if no route exists.
 */
struct child *children_lookup_route (struct children *ctx, uint32_t rank);

/* Count how many children are in an online subtree status.
 */
int children_get_online_count (struct children *ctx);

/* Update child's status.  Returns true if status changed.
 * If went_offline is non-NULL, it is set to indicate whether or not
 * there was an on online→offline transition
 */
bool children_set_status (struct children *ctx,
                          struct child *child,
                          enum subtree_status status,
                          bool *went_offline);

/* Check if child is online.
 */
bool child_is_online (struct child *child);

/* Bind to accept child connections.
 * Creates ZAP server, ROUTER socket, binds to URIs, sets up monitor.
 * Returns the concrete URIs (after wildcard expansion) via uri_out and uri2_out.
 * Caller must free the returned URI strings.
 * Returns 0 on success, -1 on error with errp set.
 */
int children_bind (struct children *ctx,
                   void *zctx,
                   struct cert *cert,
                   const char *uri,
                   const char *uri2,
                   const struct ovconf *config,
                   char **uri_out,
                   char **uri2_out,
                   flux_error_t *errp);

/* Create and start flux watcher for bind socket.
 * Returns 0 on success, -1 on error.
 */
int children_watch (struct children *ctx,
                    flux_watcher_f cb,
                    void *arg);

/* Send message to children via bind socket.
 * Returns 0 on success, -1 on error.
 */
int children_sendmsg (struct children *ctx, const flux_msg_t *msg);

/* Receive message from children via bind socket.
 * Returns message on success, NULL on error with errno set.
 */
flux_msg_t *children_recvmsg (struct children *ctx);

#endif /* !_FLUX_OVERLAY_CHILDREN_H */

// vi:ts=4 sw=4 expandtab
