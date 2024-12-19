/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_USOCK_SERVICE_H
#define _ROUTER_USOCK_SERVICE_H

/* Create a flux_t handle representing a usock socket on 'sockpath'.
 * This can be used to embed a faux flux service in a program.
 * The server end can register message handlers as usual.
 * The client end can flux_open() local://${sockpath} and make RPCs.
 *
 * Limitations:
 * - connections from "guests" (uid != server uid) are rejected
 * - event messages may not be published or subscribed to
 * - clients may not register services
 * - rank addressing is ignored
 * - server flux_t handle requires async reactor operation
 *   (one cannot call flux_recv() in a loop and expect it to make progress)
 *
 * When a client disconnects, a request is automatically sent to the server
 * from the client's uuid.  This is similar to RFC 6 disconnects, except
 * the topic string is always "disconnect", not "<service>.disconnect".
 *
 * The handle must be closed with flux_close().
 */
flux_t *usock_service_create (flux_reactor_t *r,
                              const char *sockpath,
                              bool verbose);

// accessor for star/stop/ref/unref
flux_watcher_t *usock_service_listen_watcher (flux_t *h);

#endif // _ROUTER_USOCK_SERVICE_H

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
