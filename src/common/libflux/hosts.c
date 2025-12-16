/************************************************************\
 * Copyright 2014 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <errno.h>
#include <stdbool.h>
#include <jansson.h>
#include <flux/core.h>

#include "ccan/str/str.h"
#include "src/common/libhostlist/hostlist.h"
#include "src/common/libidset/idset.h"
#include "src/common/libutil/errprintf.h"

#include "hosts.h"

struct hosts_ctx {
    struct hostlist *hostlist;
};

static void hosts_ctx_destroy (struct hosts_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        hostlist_destroy (ctx->hostlist);
        free (ctx);
        errno = saved_errno;
    }
}

static struct hosts_ctx *hosts_ctx_create (flux_t *h)
{
    struct hosts_ctx *ctx;
    const char *val;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(val = flux_attr_get (h, "hostlist"))
        || !(ctx->hostlist = hostlist_decode (val)))
        goto error;
    return ctx;
error:
    hosts_ctx_destroy (ctx);
    return NULL;
}

/* At this time, the hostlist is cached on the first call
 * and never updated.
 */
static struct hosts_ctx *hosts_ctx_get (flux_t *h)
{
    const char *auxkey = "flux::attr_hostlist";
    struct hosts_ctx *ctx = flux_aux_get (h, auxkey);
    if (!ctx) {
        if (!(ctx = hosts_ctx_create (h))
            || flux_aux_set (h,
                             auxkey,
                             ctx,
                             (flux_free_f)hosts_ctx_destroy) < 0) {
            hosts_ctx_destroy (ctx);
            return NULL;
        }
    }
    return ctx;
}

const char *flux_get_hostbyrank (flux_t *h, uint32_t rank)
{
    struct hosts_ctx *ctx;
    const char *result;

    if (rank == FLUX_NODEID_ANY)
        return "any";
    if (rank == FLUX_NODEID_UPSTREAM)
        return "upstream";
    if (!(ctx = hosts_ctx_get (h)))
        goto error;
    if (!(result = hostlist_nth (ctx->hostlist, rank)))
        goto error;
    return result;
error:
    return "(null)";
}

int flux_get_rankbyhost (flux_t *h, const char *host)
{
    struct hosts_ctx *ctx;

    if (!(ctx = hosts_ctx_get (h)))
        return -1;
    return hostlist_find (ctx->hostlist, host);
}

char *flux_hostmap_lookup (flux_t *h,
                           const char *targets,
                           flux_error_t *errp)
{
    struct hosts_ctx *ctx;
    struct hostlist *hosts = NULL;
    struct idset *ranks = NULL;
    char *s = NULL;

    if (!(ctx = hosts_ctx_get (h))) {
        errprintf (errp, "%s", strerror (errno));
        return NULL;
    }

    if ((ranks = idset_decode (targets))) {
        unsigned int rank;
        if (!(hosts = hostlist_create ())) {
            errprintf (errp, "Out of memory");
            goto err;
        }
        rank = idset_first (ranks);
        while (rank != IDSET_INVALID_ID) {
            const char *host;
            if (!(host = hostlist_nth (ctx->hostlist, rank))) {
                errprintf (errp, "rank %u is not in host map", rank);
                goto err;
            }
            if (hostlist_append (hosts, host) < 0) {
                errprintf (errp,
                           "failed appending host %s: %s",
                           host,
                           strerror (errno));
                goto err;
            }
            rank = idset_next (ranks, rank);
        }
        if (!(s = hostlist_encode (hosts))) {
            errprintf (errp,
                       "hostlist_encode: %s",
                       strerror (errno));
            goto err;
        }
    }
    else if ((hosts = hostlist_decode (targets))) {
        const char *host;
        int rank = 0;
        if (!(ranks = idset_create (0, IDSET_FLAG_AUTOGROW))) {
            errprintf (errp, "Out of memory");
            goto err;
        }
        host = hostlist_first (hosts);
        while (host) {
            if ((rank = hostlist_find (ctx->hostlist, host)) < 0) {
                errprintf (errp, "host %s not found in host map", host);
                goto err;
            }
            if (idset_set (ranks, rank) < 0) {
                errprintf (errp,
                           "idset_set (rank=%d): %s",
                           rank,
                           strerror (errno));
                goto err;
            }
            host = hostlist_next (hosts);
        }
        if (!(s = idset_encode (ranks, IDSET_FLAG_RANGE))) {
            errprintf (errp,
                       "error encoding idset: %s",
                       strerror (errno));
            goto err;
        }
    }
    else {
        errprintf (errp, "target must be a valid idset or hostlist");
        goto err;
    }
err:
    hostlist_destroy (hosts);
    idset_destroy (ranks);
    return s;
}

// vi:ts=4 sw=4 expandtab
