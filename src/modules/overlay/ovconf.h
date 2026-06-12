/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_OVERLAY_OVCONF_H
#define _FLUX_OVERLAY_OVCONF_H

#include <flux/core.h>

struct ovconf {
    double torpid_min;      // updated dynamically
    double torpid_max;      // updated dynamically

    double tcp_user_timeout;
    double connect_timeout;

    int zmqdebug;
    int zmq_io_threads;
    int enable_ipv6;
    int child_rcvhwm;

    flux_msg_handler_t **handlers;
};


/* Initialize overlay configuration, setting configuration values in 'ovconf'.
 * Service methods for dynamic configuration update are registered.
 * N.B. ovconf is initially zeroed in ovconf_init().
 *
 * The following broker attributes may be modified:
 *   tbon.prefer-tcp
 *   tbon.interface-hint
 *   tbon.torpid-min
 *   tbon.torpid-max
 *   tbon.topo
 *   tbon.child_rcvhwm
 *   tbon.zmq_io_threads
 *   tbon.zmqdebug
 */
int ovconf_init (struct ovconf *ovconf, flux_t *h, flux_error_t *errp);

/* Finalize overlay configuration.
 * Service methods for dynamic configuration update are de-registered.
 * This is safe to call even if ovconf_init() fails.
 */
void ovconf_fini (struct ovconf *ovconf);

/* Set IPv6 enable flag in configuration.
 */
void ovconf_set_ipv6 (struct ovconf *ovconf, int enable);

#endif /* !_FLUX_OVERLAY_OVCONF_H */

// vi:ts=4 sw=4 expandtab
