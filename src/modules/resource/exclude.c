/************************************************************\
 * Copyright 2020 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* exclude.c - maintain a list of exec targets excluded from scheduling
 *
 * Post exclude/unexclude event when configured exclusion set changes.
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

struct exclude {
    struct resource_ctx *ctx;
    struct idset *idset;
};

const struct idset *exclude_get (struct exclude *exclude)
{
    return exclude->idset;
}

/* Update exclusion set and post event.
 */
int exclude_update (struct exclude *exclude,
                    const char *s,
                    flux_error_t *errp)
{
    flux_t *h = exclude->ctx->h;
    struct idset *idset = NULL;
    struct idset *add;
    struct idset *del;

    if (s) {
        if (!(idset = inventory_targets_to_ranks (exclude->ctx->inventory,
                                                  s,
                                                  errp)))
            return -1;
        if (idset_last (idset) >= exclude->ctx->size) {
            errprintf (errp, "exclusion idset is out of range");
            idset_destroy (idset);
            errno = EINVAL;
            return -1;
        }
    }
    if (rutil_idset_diff (exclude->idset, idset, &add, &del) < 0) {
        errprintf (errp, "error analyzing exclusion set update");
        idset_destroy (idset);
        return -1;
    }
    if (add) {
        char *add_s = idset_encode (add, IDSET_FLAG_RANGE);
        if (!add_s || reslog_post_pack (exclude->ctx->reslog,
                                        NULL,
                                        0.,
                                        "exclude",
                                        "{s:s}",
                                        "idset",
                                        add_s) < 0) {
            flux_log_error (h, "error posting exclude event");
        }
        free (add_s);
        idset_destroy (add);
    }
    if (del) {
        char *del_s = idset_encode (del, IDSET_FLAG_RANGE);
        if (!del_s || reslog_post_pack (exclude->ctx->reslog,
                                        NULL,
                                        0.,
                                        "unexclude",
                                        "{s:s}",
                                        "idset",
                                        del_s) < 0) {
            flux_log_error (h, "error posting unexclude event");
        }
        free (del_s);
        idset_destroy (del);
    }
    idset_destroy (exclude->idset);
    exclude->idset = idset;
    return 0;
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
            flux_log_error (ctx->h,
                            "error decoding exclude set %s: %s",
                            exclude_idset,
                            error.text);
            goto error;
        }
        if (idset_last (exclude->idset) >= exclude->ctx->size) {
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
