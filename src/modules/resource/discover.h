/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_DISCOVER_H
#define _FLUX_RESOURCE_DISCOVER_H

struct discover *discover_create (struct resource_ctx *ctx);
void discover_destroy (struct discover *discover);

/* Notify this module of a change in exec target availability.
 * (internally, flux hwloc reload is not run until all ranks are online)
 */
void discover_set_available (struct discover *discover,
                             const struct idset *ids);

/* Fetch resource object.
 * If KVS lookup is in progress, block until it completes.
 * If KVS lookup is not started, return NULL.
 * (If it returns NULL, then retry after 'discover' is posted to eventlog)
 */
const json_t *discover_get (struct discover *discover);

#endif /* !_FLUX_RESOURCE_DISCOVER_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
