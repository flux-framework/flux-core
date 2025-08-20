/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* drain.c - wait for queue to become empty
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "drain.h"
#include "submit.h"
#include "alloc.h"
#include "event.h"

struct drain {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    struct flux_msglist *drain_requests;
    struct flux_msglist *idle_requests;
};

/* Drain and/or idle conditions MAY have been met.
 *
 * Since a use case may be to fetch job data from the KVS after a drain
 * or idle request completes, hand the response off to the "event batch"
 * subsystem so that the response is deferred until any batched KVS commits
 * have completed.  If there are none, e.g. the batch only contains this,
 * then the response is sent after the batch timer expires.
 */
void drain_check (struct drain *drain)
{
    const flux_msg_t *msg;
    flux_msg_t *rsp;

    /* Drained - no active jobs
     */
    if (zhashx_size (drain->ctx->active_jobs) == 0) {
        while ((msg = flux_msglist_pop (drain->drain_requests))) {
            if (!(rsp = flux_response_derive (msg, 0))
                || event_batch_respond (drain->ctx->event, rsp) < 0)
                flux_log_error (drain->ctx->h,
                                "error handing drain request off");
            flux_msg_decref (rsp);
            flux_msg_decref (msg);
        }
    }

    /* Idle - no jobs in RUN or CLEANUP state, and no pending alloc requests.
     */
    if (alloc_pending_count (drain->ctx->alloc) == 0
        && drain->ctx->running_jobs == 0) {
        int pending = zhashx_size (drain->ctx->active_jobs)
                                 - drain->ctx->running_jobs;
        while ((msg = flux_msglist_pop (drain->idle_requests))) {
            if (!(rsp = flux_response_derive (msg, 0))
                || flux_msg_pack (rsp, "{s:i}", "pending", pending) < 0
                || event_batch_respond (drain->ctx->event, rsp) < 0)
                flux_log_error (drain->ctx->h,
                                "error handing idle request off");
            flux_msg_decref (rsp);
            flux_msg_decref (msg);
        }
    }

}

static void drain_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_msglist_append (ctx->drain->drain_requests, msg) < 0)
        goto error;
    drain_check (ctx->drain);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void idle_cb (flux_t *h, flux_msg_handler_t *mh,
                     const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_msglist_append (ctx->drain->idle_requests, msg) < 0)
        goto error;
    drain_check (ctx->drain);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void destroy_requests (flux_t *h, struct flux_msglist *msglist)
{
    if (msglist) {
        const flux_msg_t *msg;
        while ((msg = flux_msglist_pop (msglist))) {
            if (flux_respond_error (h,
                                    msg,
                                    ENOSYS,
                                    "job-manager is unloading") < 0)
                flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
            flux_msg_decref (msg);
        }
    }
}

void drain_ctx_destroy (struct drain *drain)
{
    if (drain) {
        int saved_errno = errno;
        flux_msg_handler_delvec (drain->handlers);
        destroy_requests (drain->ctx->h, drain->drain_requests);
        flux_msglist_destroy (drain->drain_requests);
        destroy_requests (drain->ctx->h, drain->idle_requests);
        flux_msglist_destroy (drain->idle_requests);
        free (drain);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.drain", drain_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.idle", idle_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct drain *drain_ctx_create (struct job_manager *ctx)
{
    struct drain *drain;

    if (!(drain = calloc (1, sizeof (*drain))))
        return NULL;
    drain->ctx = ctx;
    if (!(drain->drain_requests = flux_msglist_create ())
        || !(drain->idle_requests = flux_msglist_create ()))
        goto error;
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
