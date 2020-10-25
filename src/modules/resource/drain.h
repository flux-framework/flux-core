/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_DRAIN_H
#define _FLUX_RESOURCE_DRAIN_H

struct drain *drain_create (struct resource_ctx *ctx, const json_t *eventlog);
void drain_destroy (struct drain *drain);

const struct idset *drain_get (struct drain *drain);

/* Drain 'rank' for 'reason'.  Call this on rank 0 only, otherwise use
 * resource.drain RPC.
 */
int drain_rank (struct drain *drain, uint32_t rank, const char *reason);

#endif /* !_FLUX_RESOURCE_DRAIN_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
