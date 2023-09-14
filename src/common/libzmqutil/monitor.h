/************************************************************\
 * Copyright 2021 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#include <flux/core.h>
#include <czmq.h>
#include <zmq.h>

struct monitor_event {
    uint16_t event;
    uint32_t value;
    char endpoint[256];
    const char *event_str;
    const char *value_str;
};

struct zmqutil_monitor;

typedef void (*zmqutil_monitor_f)(struct zmqutil_monitor *mon, void *arg);

/* Arrange for 'fun' to be called each time there is an event on 'sock'.
 * Create must be called before connect/bind, and destroy must be called
 * after close/destroy.
 * N.B. this will fail if an old/buggy version of libzmq is used.
 */
struct zmqutil_monitor *zmqutil_monitor_create (void *zctx,
                                                void *sock,
                                                flux_reactor_t *r,
                                                zmqutil_monitor_f fun,
                                                void *arg);
void zmqutil_monitor_destroy (struct zmqutil_monitor *mon);

/* Receive an event from the monitor socket.
 * This should be called once each time the the monitor callback is invoked.
 */
int zmqutil_monitor_get (struct zmqutil_monitor *mon,
                         struct monitor_event *mevent);

/* Returns true if the socket event likely should be logged at error severity.
 */
bool zmqutil_monitor_iserror (struct monitor_event *mevent);

// vi:ts=4 sw=4 expandtab
