/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_RESOURCE_H
#define _FLUX_RESOURCE_H

struct resource_ctx {
    flux_t *h;
    flux_msg_handler_t **handlers;
    struct inventory *inventory;
    struct monitor *monitor;
    struct topo *topology;
    struct drain *drain;
    struct exclude *exclude;
    struct acquire *acquire;
    struct reslog *reslog;

    uint32_t rank;
    uint32_t size;
};

#endif /* !_FLUX_RESOURCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
