/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* drain.c - support queue drain/undrain
 *
 * The "flux job drain" command can be used to disable job submission,
 * then wait until the queue is empty.
 *
 * Use case:  initial program submits work, then calls "flux job drain"
 * before exiting.
 *
 * Job submission can be re-enabled using "flux job undrain".
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "drain.h"
#include "submit.h"

struct drain {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlist_t *requests;
};

static void drain_complete_cb (struct queue *queue, void *arg)
{
    struct job_manager *ctx = arg;
    const flux_msg_t *msg;

    while ((msg = zlist_pop (ctx->drain->requests))) {
        if (flux_respond (ctx->h, msg, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
        flux_msg_decref (msg);
    }
}

static void drain_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (zlist_append (ctx->drain->requests,
                      (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errno = ENOMEM;
        goto error;
    }
    submit_disable (ctx->submit);
    /* N.B. If queue is empty, calls drain_complete_cb() immediately */
    queue_set_notify_empty (ctx->queue, drain_complete_cb, ctx);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void undrain_cb (flux_t *h, flux_msg_handler_t *mh,
                        const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    const flux_msg_t *req;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        return;
    }
    submit_enable (ctx->submit);
    while ((req = zlist_pop (ctx->drain->requests))) {
        if (flux_respond_error (ctx->h, req, EINVAL,
                                "queue was re-enabled") < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        flux_msg_decref (req);
    }
    if (flux_respond (ctx->h, msg, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
}

void drain_ctx_destroy (struct drain *drain)
{
    if (drain) {
        int saved_errno = errno;
        flux_msg_handler_delvec (drain->handlers);
        if (drain->requests) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (drain->requests))) {
                if (flux_respond_error (drain->ctx->h, msg, ENOSYS,
                                        "job-manager is unloading") < 0)
                    flux_log_error (drain->ctx->h, "%s: flux_respond_error",
                                    __FUNCTION__);
                flux_msg_decref (msg);
            }
            zlist_destroy (&drain->requests);
        }
        free (drain);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.drain", drain_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.undrain", undrain_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct drain *drain_ctx_create (struct job_manager *ctx)
{
    struct drain *drain;

    if (!(drain = calloc (1, sizeof (*drain))))
        return NULL;
    drain->ctx = ctx;
    if (!(drain->requests = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &drain->handlers) < 0)
        goto error;
    return drain;
error:
    drain_ctx_destroy (drain);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
