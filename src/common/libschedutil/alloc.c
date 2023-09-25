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
#include <jansson.h>

#include "schedutil_private.h"
#include "init.h"
#include "alloc.h"

static int schedutil_alloc_respond (flux_t *h,
                                    const flux_msg_t *msg,
                                    int type,
                                    const char *note,
                                    json_t *annotations)
{
    flux_jobid_t id;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return -1;
    if (annotations) {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:I s:i s:O}",
                                "id", id,
                                "type", type,
                                "annotations", annotations);
    }
    else if (note) {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:I s:i s:s}",
                                "id", id,
                                "type", type,
                                "note", note);
    }
    else {
        rc = flux_respond_pack (h,
                                msg,
                                "{s:I s:i}",
                                "id", id,
                                "type", type);
    }
    return rc;
}

int schedutil_alloc_respond_annotate_pack (schedutil_t *util,
                                           const flux_msg_t *msg,
                                           const char *fmt,
                                           ...)
{
    va_list ap;
    json_t *o = NULL;
    int rc = -1;

    va_start (ap, fmt);
    if (!(o = json_vpack_ex (NULL, 0, fmt, ap))) {
        errno = EINVAL;
        goto error;
    }
    rc = schedutil_alloc_respond (util->h,
                                  msg,
                                  FLUX_SCHED_ALLOC_ANNOTATE,
                                  NULL,
                                  o);
error:
    va_end (ap);
    json_decref (o);
    return rc;
}

int schedutil_alloc_respond_deny (schedutil_t *util,
                                  const flux_msg_t *msg,
                                  const char *note)
{
    return schedutil_alloc_respond (util->h,
                                    msg,
                                    FLUX_SCHED_ALLOC_DENY,
                                    note,
                                    NULL);
}

int schedutil_alloc_respond_cancel (schedutil_t *util, const flux_msg_t *msg)
{
    return schedutil_alloc_respond (util->h,
                                    msg,
                                    FLUX_SCHED_ALLOC_CANCEL,
                                    NULL,
                                    NULL);
}

struct alloc {
    json_t *annotations;
    const flux_msg_t *msg;
    flux_kvs_txn_t *txn;
};

static void alloc_destroy (struct alloc *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_kvs_txn_destroy (ctx->txn);
        flux_msg_decref (ctx->msg);
        json_decref (ctx->annotations);
        free (ctx);
        errno = saved_errno;
    }
}

static struct alloc *alloc_create (const flux_msg_t *msg,
                                   const char *R,
                                   const char *fmt,
                                   va_list ap)
{
    struct alloc *ctx;
    flux_jobid_t id;
    char key[64];

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return NULL;
    if (flux_job_kvs_key (key, sizeof (key), id, "R") < 0) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->msg = flux_msg_incref (msg);
    if (fmt) {
        if (!(ctx->annotations = json_vpack_ex (NULL, 0, fmt, ap)))
            goto error;
    }
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
    schedutil_t *util = arg;
    flux_t *h = util->h;
    struct alloc *ctx = flux_future_aux_get (f, "flux::alloc_ctx");

    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit R");
        goto error;
    }
    schedutil_remove_outstanding_future (util, f);
    if (schedutil_alloc_respond (h,
                                 ctx->msg,
                                 FLUX_SCHED_ALLOC_SUCCESS,
                                 NULL,
                                 ctx->annotations) < 0) {
        flux_log_error (h, "alloc response");
        goto error;
    }
    flux_future_destroy (f);
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (h)); // XXX
    alloc_destroy (ctx);
    flux_future_destroy (f);
}

int schedutil_alloc_respond_success_pack (schedutil_t *util,
                                          const flux_msg_t *msg,
                                          const char *R,
                                          const char *fmt,
                                          ...)
{
    struct alloc *ctx;
    flux_future_t *f;
    flux_t *h = util->h;
    va_list ap;

    va_start (ap, fmt);
    ctx = alloc_create (msg, R, fmt, ap);
    va_end (ap);
    if (!ctx)
        return -1;
    if (!(f = flux_kvs_commit (h, NULL, 0, ctx->txn)))
        goto error;
    if (flux_future_aux_set (f,
                             "flux::alloc_ctx",
                             ctx,
                             (flux_free_f)alloc_destroy) < 0) {
        goto error;
    }
    if (flux_future_then (f, -1, alloc_continuation, util) < 0)
        goto error;
    schedutil_add_outstanding_future (util, f);
    return 0;
error:
    alloc_destroy (ctx);
    flux_future_destroy (f);
    return -1;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
