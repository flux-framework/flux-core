/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _BROKER_HEARTBEAT_H
#define _BROKER_HEARTBEAT_H

#include "attr.h"

/* Manage the session heartbeat.
 *
 * All ranks should call heartbeat_start() to install reactor watchers.
 * On rank 0 only, this registers a reactor timer watcher which sends
 * the reactor event message.
 */

typedef struct heartbeat heartbeat_t;

heartbeat_t *heartbeat_create (void);
void heartbeat_destroy (heartbeat_t *hb);

/* Returns -1, EINVAL if rate is out of range (0.1, 30). */
int heartbeat_set_rate (heartbeat_t *hb, double rate);
double heartbeat_get_rate (heartbeat_t *hb);

void heartbeat_set_flux (heartbeat_t *hb, flux_t *h);

int heartbeat_start (heartbeat_t *hb);
void heartbeat_stop (heartbeat_t *hb);

#endif /* !_BROKER_HEARTBEAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
