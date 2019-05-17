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
#    define _BROKER_HEARTBEAT_H

#    include "attr.h"

/* Manage the session heartbeat.
 *
 * All ranks should call heartbeat_start() to install reactor watchers.
 * On rank 0 only, this registers a reactor timer watcher which sends
 * the reactor event message.
 *
 * The heartbeat_get_epoch() getter obtains the most recently processed epoch.
 *
 * Note: rank 0's epoch update is driven by the receipt of the
 * heartbeat event, not its generation.
 */

typedef struct heartbeat_struct heartbeat_t;

heartbeat_t *heartbeat_create (void);
void heartbeat_destroy (heartbeat_t *hb);

/* Default heart rate (seconds) can be set from a command line argument
 * that includes an optional "s" or "ms" unit suffix.
 * Returns -1, EINVAL if rate is out of range (0.1, 30).
 */
int heartbeat_set_ratestr (heartbeat_t *hb, const char *s);

int heartbeat_set_rate (heartbeat_t *hb, double rate);
double heartbeat_get_rate (heartbeat_t *hb);

void heartbeat_set_flux (heartbeat_t *hb, flux_t *h);
int heartbeat_set_attrs (heartbeat_t *hb, attr_t *attrs);

void heartbeat_set_epoch (heartbeat_t *hb, int epoch);
int heartbeat_get_epoch (heartbeat_t *hb);

int heartbeat_start (heartbeat_t *hb); /* rank 0 only */
void heartbeat_stop (heartbeat_t *hb);

#endif /* !_BROKER_HEARTBEAT_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
