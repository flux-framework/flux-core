/*****************************************************************************\
 *  Copyright (c) 2014 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the license, or (at your option)
 *  any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

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
        free (ctx->name);
        free (ctx);
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
        flux_aux_set (h, "flux::barrier_client", ctx, freectx);
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

int flux_barrier (flux_t *h, const char *name, int nprocs)
{
    flux_future_t *f = NULL;
    int ret = -1;

    if (!name && !(name = generate_unique_name (h)))
        goto done;
    if (!(f = flux_rpcf (h, "barrier.enter", FLUX_NODEID_ANY, 0,
                           "{s:s s:i s:i s:b}",
                           "name", name,
                           "count", 1,
                           "nprocs", nprocs,
                           "internal", false)))
        goto done;
    if (flux_future_get (f, NULL) < 0)
        goto done;
    ret = 0;
done:
    flux_future_destroy (f);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
