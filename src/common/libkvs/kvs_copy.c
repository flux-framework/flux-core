/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* flux_kvs_copy() and flux_kvs_move() return composite futures.
 *
 * Copy is implemented as a sequential lookup + put.
 *
 * Move is implemented as a sequential copy + unlink.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "kvs_copy.h"

struct copy_context {
    int commit_flags;
    char *srcns;
    char *srckey;
    char *dstns;
    char *dstkey;
};

static void copy_context_destroy (struct copy_context *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        free (ctx->srcns);
        free (ctx->srckey);
        free (ctx->dstns);
        free (ctx->dstkey);
        free (ctx);
        errno = saved_errno;
    }
}

static struct copy_context *copy_context_create (const char *srcns,
                                                 const char *srckey,
                                                 const char *dstns,
                                                 const char *dstkey,
                                                 int commit_flags)
{
    struct copy_context *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if ((srcns && !(ctx->srcns = strdup (srcns))) || !(ctx->srckey = strdup (srckey))
        || (dstns && !(ctx->dstns = strdup (dstns)))
        || !(ctx->dstkey = strdup (dstkey))) {
        copy_context_destroy (ctx);
        return NULL;
    }
    ctx->commit_flags = commit_flags;
    return ctx;
}

/* Move: unlink 'srckey' once copy finishes.
 * N.B. because copy (put) and unlink are not in the same transaction,
 * it is possible for the copy to succeed and the unlink to fail,
 * but since they are sequential, not the other way around.
 * Unwinding the copy on the error path seems just as unlikely to
 * fail, so we don't try that.  If the operations were placed in the
 * same transaction, they could not cross namespaces.
 */
static void copy_continuation (flux_future_t *f, void *arg)
{
    struct copy_context *ctx = arg;
    flux_t *h = flux_future_get_flux (f);
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f2;

    if (flux_future_get (f, NULL) < 0)
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_unlink (txn, 0, ctx->srckey) < 0)
        goto error;
    if (ctx->srcns) {
        if (!(f2 = flux_kvs_commit (h, ctx->srcns, ctx->commit_flags, txn)))
            goto error;
    } else {
        if (!(f2 = flux_kvs_commit (h, NULL, ctx->commit_flags, txn)))
            goto error;
    }
    if (flux_future_continue (f, f2) < 0) {
        flux_future_destroy (f2);
        goto error;
    }
    goto done;
error:
    flux_future_continue_error (f, errno, NULL);
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
}

/* Copy: put 'dstkey' once lookup finishes.
 * The lookup returned an RFC 11 treeobj, which could be
 * a self-contained value, or pointer(s) to content representing
 * a directory or a value.  Creating a new key with the same
 * treeobj is effectively creating a snapshot.
 */
static void lookup_continuation (flux_future_t *f, void *arg)
{
    struct copy_context *ctx = arg;
    flux_t *h = flux_future_get_flux (f);
    const char *val;
    flux_kvs_txn_t *txn = NULL;
    flux_future_t *f2;

    if (flux_kvs_lookup_get_treeobj (f, &val) < 0)
        goto error;
    if (!(txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put_treeobj (txn, 0, ctx->dstkey, val) < 0)
        goto error;
    if (ctx->dstns) {
        if (!(f2 = flux_kvs_commit (h, ctx->dstns, ctx->commit_flags, txn)))
            goto error;
    } else {
        if (!(f2 = flux_kvs_commit (h, NULL, ctx->commit_flags, txn)))
            goto error;
    }
    if (flux_future_continue (f, f2) < 0) {
        flux_future_destroy (f2);
        goto error;
    }
    goto done;
error:
    flux_future_continue_error (f, errno, NULL);
done:
    flux_future_destroy (f);
    flux_kvs_txn_destroy (txn);
}

flux_future_t *flux_kvs_copy (flux_t *h,
                              const char *srcns,
                              const char *srckey,
                              const char *dstns,
                              const char *dstkey,
                              int commit_flags)
{
    struct copy_context *ctx;
    flux_future_t *f1;
    flux_future_t *f2;

    if (!h || !srckey || !dstkey) {
        errno = EINVAL;
        return NULL;
    }
    if (srcns) {
        if (!(f1 = flux_kvs_lookup (h, srcns, FLUX_KVS_TREEOBJ, srckey)))
            return NULL;
    } else {
        if (!(f1 = flux_kvs_lookup (h, NULL, FLUX_KVS_TREEOBJ, srckey)))
            return NULL;
    }
    if (!(ctx = copy_context_create (srcns, srckey, dstns, dstkey, commit_flags)))
        goto error;
    if (flux_aux_set (h, NULL, ctx, (flux_free_f)copy_context_destroy) < 0) {
        copy_context_destroy (ctx);
        goto error;
    }
    if (!(f2 = flux_future_and_then (f1, lookup_continuation, ctx)))
        goto error;
    return f2;
error:
    flux_future_destroy (f1);
    return NULL;
}

flux_future_t *flux_kvs_move (flux_t *h,
                              const char *srcns,
                              const char *srckey,
                              const char *dstns,
                              const char *dstkey,
                              int commit_flags)
{
    struct copy_context *ctx;
    flux_future_t *f1;
    flux_future_t *f2;

    if (!h || !srckey || !dstkey) {
        errno = EINVAL;
        return NULL;
    }
    if (!(f1 = flux_kvs_copy (h, srcns, srckey, dstns, dstkey, commit_flags)))
        return NULL;
    if (!(ctx = copy_context_create (srcns, srckey, dstns, dstkey, commit_flags)))
        goto error;
    if (flux_aux_set (h, NULL, ctx, (flux_free_f)copy_context_destroy) < 0) {
        copy_context_destroy (ctx);
        goto error;
    }
    if (!(f2 = flux_future_and_then (f1, copy_continuation, ctx)))
        goto error;
    return f2;
error:
    flux_future_destroy (f1);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
