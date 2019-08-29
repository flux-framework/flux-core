/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _ROUTER_DISCONNECT_H
#define _ROUTER_DISCONNECT_H

#include <flux/core.h>

typedef void (*disconnect_send_f)(const flux_msg_t *msg, void *arg);

/* Create/destroy a disconnect notifier hash.
 * Any "armed" disconnect messages are "fired" by disconnect_destroy().
 */
struct disconnect *disconnect_create (disconnect_send_f cb, void *arg);
void disconnect_destroy (struct disconnect *dcon);

/* Arm hash with a disconnect message that will disconnect from
 * the service invoked by request 'msg'.  This function quickly returns
 * success if the disconnect is already armed for this service.
 */
int disconnect_arm (struct disconnect *dcon, const flux_msg_t *msg);

/* These functions are exposed mainly for testing.
 */
int disconnect_topic (const char *topic, char *buf, int len);
int disconnect_hashkey (const flux_msg_t *msg, char *buf, int len);
flux_msg_t *disconnect_msg (const flux_msg_t *msg);

#endif /* !_ROUTER_DISCONNECT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
