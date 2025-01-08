/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* reserve.c - get static set of resources to reserve for the OS
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/librlist/rlist.h"

#include "resource.h"
#include "inventory.h"

struct reserve {
    struct resource_ctx *ctx;
    struct rlist *rl;
};

const struct rlist *reserve_get (struct reserve *reserve)
{
    return reserve->rl;
}

void reserve_destroy (struct reserve *reserve)
{
    if (reserve) {
        int saved_errno = errno;
        rlist_destroy (reserve->rl);
        free (reserve);
        errno = saved_errno;
    }
}

struct reserve *reserve_create (struct resource_ctx *ctx,
                                const char *spec)
{
    struct reserve *reserve;
    struct rlist *rl = NULL;

    if (!(reserve = calloc (1, sizeof (*reserve))))
        return NULL;
    reserve->ctx = ctx;
    if (spec) {
        flux_error_t error;
        if (!(rl = rlist_from_json (inventory_get (ctx->inventory), NULL))) {
            flux_log (ctx->h,
                      LOG_ERR,
                      "reserve: failed to get resources from inventory");
            goto error;
        }
        if (!(reserve->rl = rlist_copy_core_spec (rl, spec, &error))) {
            flux_log (ctx->h,
                      LOG_ERR,
                      "error decoding reserve spec %s: %s",
                      spec,
                      error.text);
            goto error;
        }
        rlist_destroy (rl);
    }
    return reserve;
error:
    rlist_destroy (rl);
    reserve_destroy (reserve);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
