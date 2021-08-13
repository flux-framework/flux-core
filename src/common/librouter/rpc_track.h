/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_RPC_TRACK_H
#define _ROUTER_RPC_TRACK_H

#include <flux/core.h>

#include "msg_hash.h"

typedef void (*rpc_respond_f)(const flux_msg_t *msg, void *arg);

/* Create/destroy hash of messages.
 * Set type=MSG_HASH_TYPE_UUID_MATCHTAG.
 */
struct rpc_track *rpc_track_create (msg_hash_type_t type);
void rpc_track_destroy (struct rpc_track *rt);

/* If msg is a request that requires a response, add it to the hash.
 * If msg is a response that terminates a request in the hash (per RFC 6),
 * remove the matching request from the hash.
 * If msg is a disconnect request, remove all messages from the hash that
 * were sent by the same uuid as the disconnect request.
 */
void rpc_track_update (struct rpc_track *rt, const flux_msg_t *msg);

/* Call fun() for every hash entry, then purge all entries.
 */
void rpc_track_purge (struct rpc_track *rt, rpc_respond_f fun, void *arg);

/* Return the number of RPCs currently being tracked.
 */
int rpc_track_count (struct rpc_track *rt);

#endif /* _ROUTER_RPC_TRACK_H */

// vi:ts=4 sw=4 expandtab
