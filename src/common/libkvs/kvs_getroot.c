/*****************************************************************************\
 *  Copyright (c) 2018 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2.1 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_getroot.h"
#include "treeobj.h"

static const char *auxkey = "flux::getroot_ctx";

struct getroot_ctx {
    int rootseq;        /* seq no of cached treeobj */
    char *treeobj;      /* cached treeobj */
    int flags;          /* original flux_kvs_getroot() flags */
};

static void free_ctx (struct getroot_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx->treeobj);
        free (ctx);
        errno = saved_errno;
    }
}

static struct getroot_ctx *alloc_ctx (void)
{
    struct getroot_ctx *ctx;
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    return ctx;
}

static int validate_getroot_flags (int flags)
{
    switch (flags) {
        case 0:
        case FLUX_KVS_WATCH:
            return 0;
        default:
            return -1;
    }
}

flux_future_t *flux_kvs_getroot (flux_t *h, const char *namespace, int flags)
{
    flux_future_t *f;
    const char *topic = "kvs.getroot";
    struct getroot_ctx *ctx;

    if (!h || validate_getroot_flags (flags) < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = alloc_ctx ()))
        return NULL;
    ctx->flags = flags;
    if ((flags & FLUX_KVS_WATCH))
        topic = "kvs-watch.getroot";
    if (!namespace && !(namespace = flux_kvs_get_namespace (h)))
        goto error;
    if (!(f = flux_rpc_pack (h, topic, FLUX_NODEID_ANY, 0, "{s:s}",
                             "namespace", namespace)))
        goto error;
    if (flux_future_aux_set (f, auxkey, ctx, (flux_free_f)free_ctx) < 0)
        goto error_future;
    return f;

error_future:
    flux_future_destroy (f);
error:
    free_ctx (ctx);
    return NULL;
}

static int decode_response (flux_future_t *f, const char **rootrefp,
                            int *rootseqp, uint32_t *ownerp)
{
    const char *rootref;
    int rootseq;
    int owner;
    int flags;

    if (flux_rpc_get_unpack (f, "{s:s s:i s:i s:i}",
                             "rootref", &rootref,
                             "rootseq", &rootseq,
                             "owner", &owner,
                             "flags", &flags) < 0)
        return -1;
    if (rootrefp)
        *rootrefp = rootref;
    if (rootseqp)
        *rootseqp = rootseq;
    if (ownerp)
        *ownerp = (uint32_t)owner;
    return 0;
}

int flux_kvs_getroot_get_blobref (flux_future_t *f, const char **blobref)
{
    if (!f || !blobref) {
        errno = EINVAL;
        return -1;
    }
    return decode_response (f, blobref, NULL, NULL);
}

int flux_kvs_getroot_get_sequence (flux_future_t *f, int *rootseq)
{
    if (!f || !rootseq) {
        errno = EINVAL;
        return -1;
    }
    return decode_response (f, NULL, rootseq, NULL);
}

int flux_kvs_getroot_get_owner (flux_future_t *f, uint32_t *owner)
{
    if (!f || !owner) {
        errno = EINVAL;
        return -1;
    }
    return decode_response (f, NULL, NULL, owner);
}

/* Use cached value of ctx->treeobj, unless stored response no longer
 * contains ctx->rootseq.
 */
int flux_kvs_getroot_get_treeobj (flux_future_t *f, const char **treeobj)
{
    struct getroot_ctx *ctx;
    int rootseq;
    const char *rootref;

    if (!f || !treeobj) {
        errno = EINVAL;
        return -1;
    }
    if ((!(ctx = flux_future_aux_get (f, auxkey)))) {
        errno = EINVAL;
        return -1;
    }
    if (decode_response (f, &rootref, &rootseq, NULL) < 0)
        return -1;
    if (!ctx->treeobj || ctx->rootseq != rootseq) {
        json_t *o;
        if (!(o = treeobj_create_dirref (rootref)))
            return -1;
        free (ctx->treeobj);
        if (!(ctx->treeobj = treeobj_encode (o))) {
            json_decref (o);
            errno = ENOMEM;
            return -1;
        }
        json_decref (o);
        ctx->rootseq = rootseq;
    }
    *treeobj = ctx->treeobj;
    return 0;
}

/* This only applies with FLUX_KVS_WATCH.
 * Causes a stream of getroot responses to end with an ENODATA response.
 */
int flux_kvs_getroot_cancel (flux_future_t *f)
{
    struct getroot_ctx *ctx;
    flux_future_t *f2;

    if (!f || !(ctx = flux_future_aux_get (f, auxkey))
           || !(ctx->flags & FLUX_KVS_WATCH)) {
        errno = EINVAL;
        return -1;
    }
    if (!(f2 = flux_rpc_pack (flux_future_get_flux (f),
                              "kvs-watch.cancel",
                              FLUX_NODEID_ANY,
                              FLUX_RPC_NORESPONSE,
                              "{s:i}",
                              "matchtag", (int)flux_rpc_get_matchtag (f))))
        return -1;
    flux_future_destroy (f2);
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
