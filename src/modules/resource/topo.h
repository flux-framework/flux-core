/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_TOPO_H
#define _FLUX_RESOURCE_TOPO_H

struct topo *topo_create (struct resource_ctx *ctx,
                          struct resource_config *config);
void topo_destroy (struct topo *topo);


#endif /* !_FLUX_RESOURCE_TOPO_H */


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
