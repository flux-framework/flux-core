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

static void freectx (void *arg)
{
    ctx_t *ctx = arg;
    free (ctx);
}

static ctx_t *getctx (flux_t *h)
{
    ctx_t *ctx = flux_aux_get (h, "barriercli");
    if (!ctx) {
        const char *id = getenv ("FLUX_JOB_ID");
        if (!id && !(id = getenv ("SLURM_STEPID")))
            return NULL;
        ctx = xzmalloc (sizeof (*ctx));
        ctx->id = id;
        flux_aux_set (h, "barriercli", ctx, freectx);
    }
    return ctx;
}

int flux_barrier (flux_t *h, const char *name, int nprocs)
{
    json_object *in = Jnew ();
    char *s = NULL;
    flux_rpc_t *rpc = NULL;
    int ret = -1;

    if (!name) {
        ctx_t *ctx = getctx (h);
        if (!ctx) {
            errno = EINVAL;
            goto done;
        }
        name = s = xasprintf ("%s%d", ctx->id, ctx->seq++);
    }
    Jadd_str (in, "name", name);
    Jadd_int (in, "count", 1);
    Jadd_int (in, "nprocs", nprocs);

    if (!(rpc = flux_rpc (h, "barrier.enter", Jtostr (in), FLUX_NODEID_ANY, 0)))
        goto done;
    if (flux_rpc_get (rpc, NULL) < 0)
        goto done;
    ret = 0;
done:
    if (s)
        free (s);
    Jput (in);
    flux_rpc_destroy (rpc);
    return ret;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
