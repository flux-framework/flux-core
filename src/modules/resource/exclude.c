/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exclude.c - get static list of exec targets excluded from scheduling
 *
 * Caveats:
 * - There is no way to exclude at a finer granularity than execution target
 *   (e.g. by core would be useful).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <jansson.h>

#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"

#include "resource.h"
#include "reslog.h"
#include "exclude.h"
#include "rutil.h"
#include "inventory.h"
#include "drain.h"

struct exclude {
    struct resource_ctx *ctx;
    struct idset *idset;
};

const struct idset *exclude_get (struct exclude *exclude)
{
    return exclude->idset;
}

void exclude_destroy (struct exclude *exclude)
{
    if (exclude) {
        int saved_errno = errno;
        idset_destroy (exclude->idset);
        free (exclude);
        errno = saved_errno;
    }
}

struct exclude *exclude_create (struct resource_ctx *ctx,
                                const char *exclude_idset)
{
    struct exclude *exclude;

    if (!(exclude = calloc (1, sizeof (*exclude))))
        return NULL;
    exclude->ctx = ctx;
    if (exclude_idset) {
        flux_error_t error;
        if (!(exclude->idset = inventory_targets_to_ranks (ctx->inventory,
                                                           exclude_idset,
                                                           &error))) {
            flux_log (ctx->h,
                      LOG_ERR,
                      "error decoding exclude set %s: %s",
                      exclude_idset,
                      error.text);
            goto error;
        }
        if (idset_count (exclude->idset) > 0
            && idset_last (exclude->idset) >= exclude->ctx->size) {
            flux_log_error (ctx->h,
                            "exclude set %s is out of range",
                            exclude_idset);
            goto error;
        }
    }
    return exclude;
error:
    exclude_destroy (exclude);
    return NULL;
}


/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
