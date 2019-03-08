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
#include <stdlib.h>
#include <flux/core.h>
#include <jansson.h>

#include "ops.h"

struct ops_context {
    flux_t *h;
    flux_msg_handler_t **handlers;
    op_alloc_f *alloc_cb;
    op_free_f *free_cb;
    op_exception_f *exception_cb;
    void *arg;
};

static void alloc_continuation (flux_future_t *f, void *arg)
{
    struct ops_context *ctx = arg;
    flux_msg_t *msg = flux_future_aux_get (f, "flux::alloc_request");
    const char *jobspec;

    if (flux_kvs_lookup_get (f, &jobspec) < 0) {
        flux_log_error (ctx->h, "sched.free lookup R");
        goto error;
    }
    ctx->alloc_cb (ctx->h, msg, jobspec, ctx->arg);
    flux_future_destroy (f);
    return;
error:
    flux_log_error (ctx->h, "sched.alloc");
    if (flux_respond_error (ctx->h, msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "sched.alloc respond_error");
    flux_future_destroy (f);
}

static void alloc_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct ops_context *ctx = arg;
    flux_jobid_t id;
    char key[64];
    flux_future_t *f;
    flux_msg_t *cpy;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (flux_job_kvs_key (key, sizeof (key), true, id, "jobspec") < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        goto error;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto error_future;
    if (flux_future_aux_set (f, "flux::alloc_request",
                             cpy, (flux_free_f)flux_msg_destroy) < 0) {
        flux_msg_destroy (cpy);
        goto error_future;
    }
    if (flux_future_then (f, -1, alloc_continuation, ctx) < 0)
        goto error_future;
    return;
error_future:
    flux_future_destroy (f);
error:
    flux_log_error (h, "sched.alloc");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.alloc respond_error");
}

static void free_continuation (flux_future_t *f, void *arg)
{
    struct ops_context *ctx = arg;
    flux_msg_t *msg = flux_future_aux_get (f, "flux::free_request");
    const char *R;

    if (flux_kvs_lookup_get (f, &R) < 0) {
        flux_log_error (ctx->h, "sched.free lookup R");
        goto error;
    }
    ctx->free_cb (ctx->h, msg, R, ctx->arg);
    flux_future_destroy (f);
    return;
error:
    flux_log_error (ctx->h, "sched.alloc");
    if (flux_respond_error (ctx->h, msg, errno, NULL) < 0)
        flux_log_error (ctx->h, "sched.free respond_error");
    flux_future_destroy (f);
}

static void free_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    struct ops_context *ctx = arg;
    flux_jobid_t id;
    flux_future_t *f;
    char key[64];
    flux_msg_t *cpy;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0)
        goto error;
    if (flux_job_kvs_key (key, sizeof (key), true, id, "R") < 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(f = flux_kvs_lookup (h, NULL, 0, key)))
        goto error;
    if (!(cpy = flux_msg_copy (msg, true)))
        goto error_future;
    if (flux_future_aux_set (f, "flux::free_request",
                             cpy, (flux_free_f)flux_msg_destroy) < 0) {
        flux_msg_destroy (cpy);
        goto error_future;
    }
    if (flux_future_then (f, -1, free_continuation, ctx) < 0)
        goto error_future;
    return;
error_future:
    flux_future_destroy (f);
error:
    flux_log_error (h, "sched.free");
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "sched.free respond_error");
}

static void exception_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct ops_context *ctx = arg;
    flux_jobid_t id;
    const char *type;
    int severity;

    if (flux_event_unpack (msg, NULL, "{s:I s:s s:i}",
                                      "id", &id,
                                      "type", &type,
                                      "severity", &severity) < 0) {
        flux_log_error (h, "job-exception event");
        return;
    }
    ctx->exception_cb (h, id, type, severity, ctx->arg);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST,  "sched.alloc", alloc_cb, 0},
    { FLUX_MSGTYPE_REQUEST,  "sched.free", free_cb, 0},
    { FLUX_MSGTYPE_EVENT,  "job-exception", exception_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

/* Register dynamic service named 'sched'
 */
static int service_register (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_service_register (h, "sched")))
        return -1;
    if (flux_future_get (f, NULL) < 0) {
        flux_future_destroy (f);
        return -1;
    }
    flux_log (h, LOG_DEBUG, "service_register");
    flux_future_destroy (f);
    return 0;
}

/* Unregister dynamic service name 'sched'
 */
static void service_unregister (flux_t *h)
{
    flux_future_t *f;

    if (!(f = flux_service_unregister (h, "sched"))) {
        flux_log_error (h, "service_unregister");
        return;
    }
    if (flux_future_get (f, NULL) < 0) {
        flux_log_error (h, "service_unregister");
        flux_future_destroy (f);
        return;
    }
    flux_log (h, LOG_DEBUG, "service_unregister");
    flux_future_destroy (f);
}

struct ops_context *schedutil_ops_register (flux_t *h,
                                            op_alloc_f *alloc_cb,
                                            op_free_f *free_cb,
                                            op_exception_f *exception_cb,
                                            void *arg)
{
    struct ops_context *ctx;

    if (!h || !alloc_cb || !free_cb || !exception_cb) {
        errno = EINVAL;
        return NULL;
    }
    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    if (service_register (h) < 0)
        goto error;
    ctx->h = h;
    ctx->alloc_cb = alloc_cb;
    ctx->free_cb = free_cb;
    ctx->exception_cb = exception_cb;
    ctx->arg = arg;

    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    if (flux_event_subscribe (h, "job-exception") < 0)
        goto error;
    return ctx;
error:
    schedutil_ops_unregister (ctx);
    return NULL;
}


void schedutil_ops_unregister (struct ops_context *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        (void)service_unregister (ctx->h);
        (void)flux_event_unsubscribe (ctx->h, "job-exception");
        flux_msg_handler_delvec (ctx->handlers);
        free (ctx);
        errno = saved_errno;
    }
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
