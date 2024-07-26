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

struct monitor *monitor_create (struct resource_ctx *ctx,
                                int inventory_size,
                                bool monitor_force_up);
void monitor_destroy (struct monitor *monitor);

const struct idset *monitor_get_down (struct monitor *monitor);
const struct idset *monitor_get_up (struct monitor *monitor);

const struct idset *monitor_get_torpid (struct monitor *monitor);

#endif /* !_FLUX_RESOURCE_MONITOR_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
