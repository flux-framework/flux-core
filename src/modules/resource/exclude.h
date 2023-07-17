/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_EXCLUDE_H
#define _FLUX_RESOURCE_EXCLUDE_H

struct exclude *exclude_create (struct resource_ctx *ctx, const char *idset);
void exclude_destroy (struct exclude *exclude);

const struct idset *exclude_get (struct exclude *exclude);

#endif /* !_FLUX_RESOURCE_EXCLUDE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
