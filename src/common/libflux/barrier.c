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
#include <flux/core.h>
#include <stdbool.h>

#include "src/common/libutil/xzmalloc.h"

typedef struct {
    const char *id;
    int seq;
    char *name;
    size_t name_len;
} libbarrier_ctx_t;

static void freectx (void *arg)
{
    libbarrier_ctx_t *ctx = arg;
    if (ctx) {
        int saved_errno = errno;
        free (ctx->name);
        free (ctx);
        errno = saved_errno;
    }
}

static libbarrier_ctx_t *getctx (flux_t *h)
{
    libbarrier_ctx_t *ctx = flux_aux_get (h, "flux::barrier_client");
    if (!ctx) {
        const char *id = getenv ("FLUX_JOB_ID");
        if (!id && !(id = getenv ("SLURM_STEPID"))) {
            errno = EINVAL;
            goto error;
        }
        if (!(ctx = calloc (1, sizeof (*ctx)))) {
            errno = ENOMEM;
            goto error;
        }
        ctx->name_len = strlen (id) + 16;
        if (!(ctx->name = calloc (1, ctx->name_len))) {
            errno = ENOMEM;
            goto error;
        }
        ctx->id = id;
        if (flux_aux_set (h, "flux::barrier_client", ctx, freectx) < 0)
            goto error;
    }
    return ctx;
error:
    freectx (ctx);
    return NULL;
}

static const char *generate_unique_name (flux_t *h)
{
    libbarrier_ctx_t *ctx = getctx (h);
    if (!ctx)
        return NULL;
    int n = snprintf (ctx->name, ctx->name_len, "%s%d", ctx->id, ctx->seq++);
    if (n >= ctx->name_len) {
        errno = EINVAL;
        return NULL;
    }
    return ctx->name;
}

flux_future_t *flux_barrier (flux_t *h, const char *name, int nprocs)
{
    if (!name && !(name = generate_unique_name (h)))
        return NULL;

    return flux_rpc_pack (h,
                          "barrier.enter",
                          FLUX_NODEID_ANY,
                          0,
                          "{s:s s:i}",
                           "name", name,
                           "nprocs", nprocs);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
