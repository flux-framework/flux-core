/************************************************************\
 * Copyright 2017 Lawrence Livermore National Security, LLC
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
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "treeobj.h"
#include "kvs_txn_private.h"
#include "kvs_util_private.h"
#include "src/common/libutil/blobref.h"

static const char *auxkey = "flux::commit_ctx";

struct commit_ctx {
    char *treeobj; /* cached treeobj */
};

static void free_ctx (struct commit_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx->treeobj);
        free (ctx);
        errno = saved_errno;
    }
}

static struct commit_ctx *alloc_ctx (void)
{
    struct commit_ctx *ctx;
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    return ctx;
}

flux_future_t *flux_kvs_fence (flux_t *h,
                               const char *ns,
                               int flags,
                               const char *name,
                               int nprocs,
                               flux_kvs_txn_t *txn)
{
    flux_future_t *f;
    struct commit_ctx *ctx = NULL;
    json_t *ops;

    if (!name || nprocs <= 0 || !txn) {
        errno = EINVAL;
        return NULL;
    }

    if (!ns) {
        if (!(ns = kvs_get_namespace ()))
            return NULL;
    }

    if (!(ops = txn_get_ops (txn))) {
        errno = EINVAL;
        return NULL;
    }

    if (!(ctx = alloc_ctx ()))
        return NULL;

    if (!(f = flux_rpc_pack (h,
                             "kvs.fence",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i s:s s:i s:O}",
                             "name",
                             name,
                             "nprocs",
                             nprocs,
                             "namespace",
                             ns,
                             "flags",
                             flags,
                             "ops",
                             ops)))
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

flux_future_t *flux_kvs_commit (flux_t *h,
                                const char *ns,
                                int flags,
                                flux_kvs_txn_t *txn)
{
    flux_future_t *f;
    struct commit_ctx *ctx = NULL;
    json_t *ops;

    if (!txn) {
        errno = EINVAL;
        return NULL;
    }

    if (!ns) {
        if (!(ns = kvs_get_namespace ()))
            return NULL;
    }

    if (!(ops = txn_get_ops (txn))) {
        errno = EINVAL;
        return NULL;
    }

    if (!(ctx = alloc_ctx ()))
        return NULL;

    if (!(f = flux_rpc_pack (h,
                             "kvs.commit",
                             FLUX_NODEID_ANY,
                             0,
                             "{s:s s:i s:O}",
                             "namespace",
                             ns,
                             "flags",
                             flags,
                             "ops",
                             ops)))
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

static int decode_response (flux_future_t *f, const char **rootrefp, int *rootseqp)
{
    const char *rootref;
    int rootseq;

    if (flux_rpc_get_unpack (f, "{s:s s:i}", "rootref", &rootref, "rootseq", &rootseq)
        < 0)
        return -1;
    if (rootrefp)
        *rootrefp = rootref;
    if (rootseqp)
        *rootseqp = rootseq;
    return 0;
}

int flux_kvs_commit_get_sequence (flux_future_t *f, int *rootseq)
{
    if (!f || !rootseq) {
        errno = EINVAL;
        return -1;
    }
    return decode_response (f, NULL, rootseq);
}

/* Use cached value of ctx->treeobj */
int flux_kvs_commit_get_treeobj (flux_future_t *f, const char **treeobj)
{
    struct commit_ctx *ctx;
    const char *rootref;

    if (!f || !treeobj) {
        errno = EINVAL;
        return -1;
    }
    if ((!(ctx = flux_future_aux_get (f, auxkey)))) {
        errno = EINVAL;
        return -1;
    }
    if (decode_response (f, &rootref, NULL) < 0)
        return -1;
    if (!ctx->treeobj) {
        json_t *o;
        if (!(o = treeobj_create_dirref (rootref)))
            return -1;
        if (!(ctx->treeobj = treeobj_encode (o))) {
            json_decref (o);
            errno = ENOMEM;
            return -1;
        }
        json_decref (o);
    }
    *treeobj = ctx->treeobj;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
