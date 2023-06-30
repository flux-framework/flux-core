/************************************************************\
 * Copyright 2023 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _SDBUS_WATCHER_H
#define _SDBUS_WATCHER_H

#include <systemd/sd-bus.h>
#include <flux/core.h>

/* This watcher is called each time the sd-bus may require service.
 * The callback should call sd_bus_process(3) to give libsystemd the
 * opportunity to make progress.
 */
flux_watcher_t *sdbus_watcher_create (flux_reactor_t *r,
                                      sd_bus *bus,
                                      flux_watcher_f cb,
                                      void *arg);

#endif /* !_SDBUS_WATCHER_H */

// vi:ts=4 sw=4 expandtab
