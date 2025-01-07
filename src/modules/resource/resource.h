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

struct resource_config {
    json_t *R;
    const char *exclude_idset;
    bool rediscover;
    bool noverify;
    bool norestrict;
    bool no_update_watch;
    bool monitor_force_up;
};

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
    struct status *status;

    flux_t *parent_h;
    int parent_refcount;

    uint32_t rank;
    uint32_t size;
};

/*  Get a shared handle to he parent instance if the parent-uri attribute
 *  is set. Adds a reference to the shared parent handle. Caller must call
 *  resource_parent_handle_close().
 *
 *  Returns NULL on error with errno set to ENOENT if there is no parent-uri,
 *  or error from flux_open(3).
 */
flux_t *resource_parent_handle_open (struct resource_ctx *ctx);
void resource_parent_handle_close (struct resource_ctx *ctx);

#endif /* !_FLUX_RESOURCE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
