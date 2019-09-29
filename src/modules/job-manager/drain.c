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

struct drain_ctx {
    flux_t *h;
    struct submit_ctx *submit_ctx;
    struct queue *queue;
    flux_msg_handler_t **handlers;
    zlist_t *requests;
    bool armed;
    int inactive_count;
};

static void drain_complete (struct drain_ctx *ctx)
{
    const flux_msg_t *msg;

    while ((msg = zlist_pop (ctx->requests))) {
        if (flux_respond (ctx->h, msg, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
        flux_msg_decref (msg);
    }
}

static void job_state_cb (struct job *job, flux_job_state_t old, void *arg)
{
    struct drain_ctx *ctx = arg;

    if (ctx->armed && job->state == FLUX_JOB_INACTIVE) {
        if (++ctx->inactive_count == queue_size (ctx->queue))
            drain_complete (ctx);
    }
}

static int count_inactive_jobs (struct queue *queue)
{
    int count = 0;
    struct job *job;

    job = queue_first (queue);
    while (job) {
        if (job->state == FLUX_JOB_INACTIVE)
            count++;
        job = queue_next (queue);
    }
    return count;
}

static void drain_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct drain_ctx *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (zlist_append (ctx->requests, (void *)flux_msg_incref (msg)) < 0) {
        flux_msg_decref (msg);
        errno = ENOMEM;
        goto error;
    }
    if (!ctx->armed) {
        submit_disable (ctx->submit_ctx);
        ctx->inactive_count = count_inactive_jobs (ctx->queue);
        ctx->armed = true;
    }
    if (ctx->inactive_count == queue_size (ctx->queue))
        drain_complete (ctx);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void undrain_cb (flux_t *h, flux_msg_handler_t *mh,
                        const flux_msg_t *msg, void *arg)
{
    struct drain_ctx *ctx = arg;
    const flux_msg_t *req;

    if (flux_request_decode (msg, NULL, NULL) < 0) {
        if (flux_respond_error (h, msg, errno, NULL) < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        return;
    }
    if (ctx->armed) {
        submit_enable (ctx->submit_ctx);
        ctx->armed = false;
    }
    while ((req = zlist_pop (ctx->requests))) {
        if (flux_respond_error (ctx->h, req, EINVAL,
                                "queue was re-enabled") < 0)
            flux_log_error (ctx->h, "%s: flux_respond_error", __FUNCTION__);
        flux_msg_decref (req);
    }
    if (flux_respond (ctx->h, msg, NULL) < 0)
        flux_log_error (ctx->h, "%s: flux_respond", __FUNCTION__);
}

void drain_ctx_destroy (struct drain_ctx *ctx)
{
    if (ctx) {
        int saved_errno = errno;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->requests) {
            const flux_msg_t *msg;
            while ((msg = zlist_pop (ctx->requests))) {
                if (flux_respond_error (ctx->h, msg, ENOSYS,
                                        "job-manager is unloading") < 0)
                    flux_log_error (ctx->h, "%s: flux_respond_error",
                                    __FUNCTION__);
                flux_msg_decref (msg);
            }
            zlist_destroy (&ctx->requests);
        }
        free (ctx);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.drain", drain_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.undrain", undrain_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct drain_ctx *drain_ctx_create (flux_t *h,
                                    struct queue *queue,
                                    struct event_ctx *event_ctx,
                                    struct submit_ctx *submit_ctx)
{
    struct drain_ctx *ctx;

    if (!(ctx = calloc (1, sizeof (*ctx))))
        return NULL;
    ctx->h = h;
    ctx->queue = queue;
    ctx->submit_ctx = submit_ctx;
    if (!(ctx->requests = zlist_new ())) {
        errno = ENOMEM;
        goto error;
    }
    if (flux_msg_handler_addvec (h, htab, ctx, &ctx->handlers) < 0)
        goto error;
    event_ctx_set_state_cb (event_ctx, job_state_cb, ctx);
    return ctx;
error:
    drain_ctx_destroy (ctx);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
