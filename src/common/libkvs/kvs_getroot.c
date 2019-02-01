/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
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
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <jansson.h>
#include <czmq.h>
#include <flux/core.h>

#include "kvs_getroot.h"
#include "kvs_util_private.h"
#include "treeobj.h"

static const char *auxkey = "flux::getroot_ctx";

struct getroot_ctx {
    char *treeobj;      /* cached treeobj */
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

flux_future_t *flux_kvs_getroot (flux_t *h, const char *ns, int flags)
{
    flux_future_t *f;
    struct getroot_ctx *ctx;

    if (!h || flags) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = alloc_ctx ()))
        return NULL;
    if (!ns && !(ns = kvs_get_namespace ()))
        goto error;
    if (!(f = flux_rpc_pack (h, "kvs.getroot", FLUX_NODEID_ANY, 0,
                             "{s:s s:i}",
                             "namespace", ns,
                             "flags", flags)))
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

/* Use cached value of ctx->treeobj */
int flux_kvs_getroot_get_treeobj (flux_future_t *f, const char **treeobj)
{
    struct getroot_ctx *ctx;
    const char *rootref;

    if (!f || !treeobj) {
        errno = EINVAL;
        return -1;
    }
    if ((!(ctx = flux_future_aux_get (f, auxkey)))) {
        errno = EINVAL;
        return -1;
    }
    if (decode_response (f, &rootref, NULL, NULL) < 0)
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
