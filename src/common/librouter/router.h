/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_ROUTER_H
#define _ROUTER_ROUTER_H

#include "subhash.h"

struct router;
struct router_entry;

/* router > client
 *  e.g. calls usock_conn_send()
 */
typedef int (*router_entry_send_f)(const flux_msg_t *msg, void *arg);

/* router < client
 *   e.g. usock_conn_recv_f calls this
 */
void router_entry_recv (struct router_entry *entry, flux_msg_t *msg);

/* Upon client connect, add route entry.
 * Returned entry can be used with router_entry_recv() and should be
 * destroyed when client disconnects (e.g. store in usock_conn aux hash).
 */
struct router_entry *router_entry_add (struct router *rtr,
                                       const char *uuid,
                                       router_entry_send_f cb,
                                       void *arg);
void router_entry_delete (struct router_entry *entry);

/* Notify router that connection was lost to broker and restored
 * so it can re-establish event subscriptions and service registrations.
 * It does this synchronously.
 */
int router_renew (struct router *rtr);

/* Create/destroy router.  'h' is the "upstream broker connection.
 */
struct router *router_create (flux_t *h);
void router_destroy (struct router *rtr);

/* Avoid unsubscribe deadlock during broker shutdown - issue #1025
 */
void router_mute (struct router *rtr);


#endif /* !_ROUTER_ROUTER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
