/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_OVERLAY_PARENT_H
#define _FLUX_OVERLAY_PARENT_H

#include <flux/core.h>
#include "src/common/librouter/rpc_track.h"
#include "src/common/libzmqutil/monitor.h"
#include "src/common/libzmqutil/cert.h"
#include "ovconf.h"

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37
#endif

/* Parent peer state */
struct parent {
    void *zsock;            // NULL on rank 0
    char *uri;
    flux_watcher_t *w;
    int lastsent;
    char *pubkey;
    uint32_t rank;
    char uuid[UUID_STR_LEN];
    bool hello_error;
    bool hello_responded;
    bool offline;           // set upon receipt of CONTROL_DISCONNECT
    bool goodbye_sent;
    struct rpc_track *tracker;
    struct zmqutil_monitor *monitor;
    flux_t *h;              // borrowed reference for logging
};

/* Create parent state.
 * Call this when topology is set for rank > 0.
 * Returns pointer on success, NULL on error with errno set.
 */
struct parent *parent_create (flux_t *h, uint32_t rank);

/* Destroy parent state.
 */
void parent_destroy (struct parent *parent);

/* Set parent URI (copied).
 * Returns 0 on success, -1 on error.
 */
int parent_set_uri (struct parent *parent, const char *uri);

/* Set parent public key (copied).
 * Returns 0 on success, -1 on error.
 */
int parent_set_pubkey (struct parent *parent, const char *pubkey);

/* Check if parent connection has an error.
 * Returns true if hello_error or offline.
 */
bool parent_error (struct parent *parent);

/* Connect to parent peer.
 * Creates ZMQ DEALER socket, sets options, applies cert, and connects.
 * Sets up monitor if zmqdebug is enabled.
 * Returns 0 on success, -1 on error.
 */
int parent_connect (struct parent *parent,
                    void *zctx,
                    struct cert *cert,
                    const char *uuid,
                    const struct ovconf *config);

/* Disconnect from parent peer.
 * Calls zmq_disconnect and sets offline flag.
 */
void parent_disconnect (struct parent *parent);

/* Create and start flux watcher for parent socket.
 * Returns 0 on success, -1 on error.
 */
int parent_watch (struct parent *parent, flux_watcher_f cb, void *arg);

/* Send message to parent.
 * Checks parent state (offline, goodbye_sent) and updates lastsent timestamp.
 * Returns 0 on success, -1 with errno=EHOSTUNREACH if parent unavailable.
 */
int parent_sendmsg (struct parent *parent, const flux_msg_t *msg);

/* Receive message from parent.
 * Returns message on success, NULL on error with errno set.
 */
flux_msg_t *parent_recvmsg (struct parent *parent);

/* Set hello exchange status (responded and whether it was an error).
 */
void parent_set_hello_responded (struct parent *parent, bool error);

/* Check if hello exchange has completed (success or error).
 * Returns true if hello_responded is set.
 */
bool parent_hello_responded (struct parent *parent);

/* Check if goodbye has been sent.
 * Returns true if goodbye_sent is set.
 */
bool parent_goodbye_sent (struct parent *parent);

/* Set goodbye_sent flag to prevent duplicate sends.
 */
void parent_set_goodbye_sent (struct parent *parent);

#endif /* !_FLUX_OVERLAY_PARENT_H */

// vi:ts=4 sw=4 expandtab
