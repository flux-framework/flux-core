/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_MONITOR_H
#define _FLUX_RESOURCE_MONITOR_H

typedef void (*monitor_cb_f)(struct monitor *monitor, void *arg);

struct monitor *monitor_create (struct resource_ctx *ctx,
                                bool monitor_force_up);
void monitor_destroy (struct monitor *monitor);
void monitor_set_callback (struct monitor *monitor, monitor_cb_f cb, void *arg);

const struct idset *monitor_get_down (struct monitor *monitor);
const struct idset *monitor_get_up (struct monitor *monitor);

#endif /* !_FLUX_RESOURCE_MONITOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
