/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_RESERVE_H
#define _FLUX_RESOURCE_RESERVE_H

struct reserve *reserve_create (struct resource_ctx *ctx, const char *spec);
void reserve_destroy (struct reserve *exclude);

const struct rlist *reserve_get (struct reserve *reserve);

#endif /* !_FLUX_RESOURCE_RESERVE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

