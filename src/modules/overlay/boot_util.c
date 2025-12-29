/************************************************************\
 * Copyright 2025 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* boot_util.c - bootstrap RPCs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/errno_safe.h"

#include "boot_util.h"

int boot_util_iam (flux_t *h, const struct bizcard *bc, flux_error_t *errp)
{
    flux_future_t *f;
    if (!(f = flux_rpc_pack (h,
                             "bootstrap.iam",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:O}",
                             "bizcard", bizcard_get_json (bc)))
        || flux_rpc_get (f, NULL) < 0) {
        errprintf (errp, "bootstrap.iam: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

int boot_util_barrier (flux_t *h, flux_error_t *errp)
{
    flux_future_t *f;
    if (!(f = flux_rpc (h, "bootstrap.barrier", NULL, FLUX_NODEID_ANY, 0))
        || flux_rpc_get (f, NULL) < 0) {
        errprintf (errp, "bootstrap.barrier: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        return -1;
    }
    flux_future_destroy (f);
    return 0;
}

struct bizcard *boot_util_whois_rank (flux_t *h, int rank, flux_error_t *errp)
{
    flux_future_t *f;
    json_t *o;
    struct bizcard *bc = NULL;

    if (!(f = flux_rpc_pack (h,
                             "bootstrap.whois",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:i}",
                             "ranks", rank))
        || flux_rpc_get_unpack (f, "{s:o}", "bizcard", &o) < 0) {
        errprintf (errp, "bootstrap.whois: %s", future_strerror (f, errno));
        goto done;
    }
    if (!(bc = bizcard_fromjson (o))) {
        errprintf (errp, "bootstrap.whois: %s", strerror (errno));
        goto done;
    }
done:
    flux_future_destroy (f);
    return bc;
}

flux_future_t *boot_util_whois (flux_t *h,
                                int *ranks,
                                int count,
                                flux_error_t *errp)
{
    struct idset *ids;
    flux_future_t *f;
    char *peers = NULL;

    if (!(ids = idset_create (0, IDSET_FLAG_AUTOGROW)))
        goto error_idset;
    for (int i = 0; i < count; i++) {
        if (idset_set (ids, ranks[i]) < 0)
            goto error_idset;
    }
    if (!(peers = idset_encode (ids, IDSET_FLAG_RANGE)))
        goto error_idset;
    if (!(f = flux_rpc_pack (h,
                             "bootstrap.whois",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:s}",
                             "ranks", peers))) {
        errprintf (errp, "bootstrap.whois: %s", future_strerror (f, errno));
        idset_destroy (ids);
        ERRNO_SAFE_WRAP (free, peers);
        return NULL;
    }
    free (peers);
    idset_destroy (ids);
    return f;
error_idset:
    idset_destroy (ids);
    ERRNO_SAFE_WRAP (free, peers);
    errprintf (errp, "error creating idset: %s", strerror (errno));
    return NULL;
}

struct bizcard *boot_util_whois_get_bizcard (flux_future_t *f)
{
    json_t *o;
    struct bizcard *bc;

    if (flux_rpc_get_unpack (f, "{s:o}", "bizcard", &o) < 0
        || !(bc = bizcard_fromjson (o)))
        return NULL;
    return bc;
}

int boot_util_whois_get_rank (flux_future_t *f)
{
    int rank;
    if (flux_rpc_get_unpack (f, "{s:i}", "rank", &rank) < 0)
        return -1;
    return rank;
}

int boot_util_finalize (flux_t *h, struct idset *critical_ranks, flux_error_t *errp)
{
    char *crit;
    flux_future_t *f;

    if (!(crit = idset_encode (critical_ranks, IDSET_FLAG_RANGE))) {
        errprintf (errp,
                   "error calculating default critical ranks: %s",
                   strerror (errno));
        return -1;
    }
    if (!(f = flux_rpc_pack (h,
                             "bootstrap.finalize",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s}",
                             "crit", crit))
        || flux_rpc_get (f, NULL) < 0) {
        errprintf (errp, "bootstrap.finalize: %s", future_strerror (f, errno));
        flux_future_destroy (f);
        free (crit);
        return -1;
    }
    free (crit);
    flux_future_destroy (f);
    return 0;
}


// vi:ts=4 sw=4 expandtab
