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

#include "src/common/libutil/errno_safe.h"
#include "schedutil_private.h"
#include "init.h"
#include "alloc.h"

static int schedutil_alloc_respond_pack (flux_t *h,
                                         const flux_msg_t *msg,
                                         int type,
                                         const char *fmt,
                                         ...)
{
    flux_jobid_t id;
    va_list ap;
    json_t *payload;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        return -1;
    if (!(payload = json_pack ("{s:I s:i}",
                               "id", id,
                               "type", type)))
        goto nomem;
    if (fmt) {
        json_t *o;
        va_start (ap, fmt);
        o = json_vpack_ex (NULL, 0, fmt, ap);
        va_end (ap);
        if (!o || json_object_update (payload, o) < 0) {
            json_decref (o);
            goto nomem;
        }
        json_decref (o);
    }
    if (flux_respond_pack (h, msg, "O", payload) < 0)
        goto error;
    json_decref (payload);
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, payload);
    return -1;
}

int schedutil_alloc_respond_annotate_pack (schedutil_t *util,
                                           const flux_msg_t *msg,
                                           const char *fmt,
                                           ...)
{
    va_list ap;
    json_t *annotations;
    int rc;

    va_start (ap, fmt);
    annotations = json_vpack_ex (NULL, 0, fmt, ap);
    va_end (ap);
    if (!annotations) {
        errno = EINVAL;
        return -1;
    }
    rc = schedutil_alloc_respond_pack (util->h,
                                       msg,
                                       FLUX_SCHED_ALLOC_ANNOTATE,
                                       "{s:O}",
                                       "annotations", annotations);
    ERRNO_SAFE_WRAP (json_decref, annotations);
    return rc;
}

int schedutil_alloc_respond_deny (schedutil_t *util,
                                  const flux_msg_t *msg,
                                  const char *note)
{
    if (note) {
        return schedutil_alloc_respond_pack (util->h,
                                             msg,
                                             FLUX_SCHED_ALLOC_DENY,
                                             "{s:s}",
                                             "note", note);
    }
    return schedutil_alloc_respond_pack (util->h,
                                         msg,
                                         FLUX_SCHED_ALLOC_DENY,
                                         NULL);
}

int schedutil_alloc_respond_cancel (schedutil_t *util, const flux_msg_t *msg)
{
    return schedutil_alloc_respond_pack (util->h,
                                         msg,
                                         FLUX_SCHED_ALLOC_CANCEL,
                                         NULL);
}

struct alloc {
    json_t *annotations;
    const flux_msg_t *msg;
    flux_kvs_txn_t *txn;
    json_t *R;
};

static void alloc_destroy (struct alloc *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_kvs_txn_destroy (ctx->txn);
        flux_msg_decref (ctx->msg);
        json_decref (ctx->annotations);
        json_decref (ctx->R);
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
    if (!(ctx->R = json_loads (R, 0, NULL))) {
        errno = ENOMEM;
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
    json_t *payload = NULL;

    schedutil_remove_outstanding_future (util, f);
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "commit R");
        goto error;
    }
    if (!(payload = json_object ())
        || (ctx->annotations && json_object_set (payload,
                                                 "annotations",
                                                 ctx->annotations) < 0)
        || json_object_set (payload, "R", ctx->R) < 0) {
        errno = ENOMEM;
        flux_log_error (h, "error responding to alloc request");
        goto error;
    }
    if (schedutil_alloc_respond_pack (h,
                                      ctx->msg,
                                      FLUX_SCHED_ALLOC_SUCCESS,
                                      "O",
                                      payload) < 0) {
        flux_log_error (h, "error responding to alloc request");
        goto error;
    }
    json_decref (payload);
    flux_future_destroy (f);
    return;
error:
    flux_reactor_stop_error (flux_get_reactor (h)); // XXX
    ERRNO_SAFE_WRAP (json_decref, payload);
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
