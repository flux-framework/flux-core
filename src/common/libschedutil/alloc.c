/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
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

#include "alloc.h"

int schedutil_alloc_request_decode (const flux_msg_t *msg,
                                    flux_jobid_t *id,
                                    int *priority,
                                    uint32_t *userid,
                                    double *t_submit)
{
    return flux_request_unpack (msg, NULL, "{s:I s:i s:i s:f}",
                                           "id", id,
                                           "priority", priority,
                                           "userid", userid,
                                           "t_submit", t_submit);
}

static int schedutil_alloc_respond (flux_t *h, const flux_msg_t *msg,
                                    int type, const char *note)
{
    flux_jobid_t id;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return -1;
    if (note)
        rc = flux_respond_pack (h, msg, "{s:I s:i s:s}",
                                        "id", id,
                                        "type", type,
                                        "note", note);
    else
        rc = flux_respond_pack (h, msg, "{s:I s:i}",
                                        "id", id,
                                        "type", type);
    return rc;
}

int schedutil_alloc_respond_note (flux_t *h, const flux_msg_t *msg,
                                  const char *note)
{
    return schedutil_alloc_respond (h, msg, 1, note);
}

int schedutil_alloc_respond_denied (flux_t *h, const flux_msg_t *msg,
                                    const char *note)
{
    return schedutil_alloc_respond (h, msg, 2, note);
}

struct alloc {
    char *note;
    flux_msg_t *msg;
    flux_kvs_txn_t *txn;
};

static void alloc_destroy (struct alloc *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_kvs_txn_destroy (ctx->txn);
        flux_msg_destroy (ctx->msg);
        free (ctx->note);
        free (ctx);
        errno = saved_errno;
    }
}

static struct alloc *alloc_create (const flux_msg_t *msg, const char *R,
                                   const char *note)
{
    struct alloc *ctx;
    flux_jobid_t id;
    char key[64];

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return NULL;
    if (flux_job_kvs_key (key, sizeof (key), true, id, "R") < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (!(ctx->msg = flux_msg_copy (msg, true)))
        goto error;
    if (note && !(ctx->note = strdup (note)))
        goto error;
    if (!(ctx->txn = flux_kvs_txn_create ()))
        goto error;
    if (flux_kvs_txn_put (ctx->txn, 0, key, R) < 0)
        goto error;
    return ctx;
error:
    alloc_destroy (ctx);
    return NULL;
}

static void alloc_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct alloc *ctx = arg;

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit R");
        goto error;
    }
    if (schedutil_alloc_respond (h, ctx->msg, 0, ctx->note) < 0) {
        flux_log_error (h, "alloc response");
        goto error;
    }
    alloc_destroy (ctx);
    flux_future_destroy (f);
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (h)); // XXX
    alloc_destroy (ctx);
    flux_future_destroy (f);
}

int schedutil_alloc_respond_R (flux_t *h, const flux_msg_t *msg,
                               const char *R, const char *note)
{
    struct alloc *ctx;
    flux_future_t *f;

    if (!(ctx = alloc_create (msg, R, note)))
        return -1;
    if (!(f = flux_kvs_commit (h, NULL, 0, ctx->txn)))
        goto error;
    if (flux_future_then (f, -1, alloc_continuation, ctx) < 0)
        goto error;
    return 0;
error:
    alloc_destroy (ctx);
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
