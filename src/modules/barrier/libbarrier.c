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

#include "src/common/libutil/xzmalloc.h"
#include "src/common/libutil/shortjson.h"

typedef struct {
    const char *id;
    int seq;
} ctx_t;

static void freectx (ctx_t *ctx)
{
    free (ctx);
}

static ctx_t *getctx (flux_t h)
{
    ctx_t *ctx = flux_aux_get (h, "barriercli");
    if (!ctx) {
        const char *id = getenv ("FLUX_LWJ_ID");
        if (!id && !(id = getenv ("SLURM_STEPID")))
            return NULL;
        ctx = xzmalloc (sizeof (*ctx));
        ctx->id = id;
        flux_aux_set (h, "barriercli", ctx, (FluxFreeFn)freectx);
    }
    return ctx;
}

int flux_barrier (flux_t h, const char *name, int nprocs)
{
    JSON request = Jnew ();
    char *s = NULL;
    int ret = -1;

    if (!name) {
        ctx_t *ctx = getctx (h);
        if (!ctx) {
            errno = EINVAL;
            goto done;
        }
        name = s = xasprintf ("%s%d", ctx->id, ctx->seq++);
    }
    Jadd_str (request, "name", name);
    Jadd_int (request, "count", 1);
    Jadd_int (request, "nprocs", nprocs);

    if (flux_json_rpc (h, FLUX_NODEID_ANY, "barrier.enter", request, NULL) < 0)
        goto done;
    ret = 0;
done:
    if (s)
        free (s);
    Jput (request);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
