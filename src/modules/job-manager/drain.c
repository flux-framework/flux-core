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
#include <czmq.h>
#include <flux/core.h>

#include "src/common/libutil/errno_safe.h"

#include "drain.h"
#include "submit.h"
#include "event.h"

struct drain {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zlist_t *requests;
};

/* Drain conditions have been met.
 * To avoid sending drain responses before the events that triggered the
 * job state transitions to INACTIVE are committed to the KVS, hand off the
 * drain request to the "event batch" subsystem, so the response can be sent
 * at the same time as job state change notifications are published,
 * after the KVS commit completes.
 */
void drain_empty_notify (struct drain *drain)
{
    const flux_msg_t *msg;

    while ((msg = zlist_pop (drain->requests))) {
        if (event_batch_drain_respond (drain->ctx->event, msg) < 0)
            flux_log_error (drain->ctx->h, "error handing drain request off");
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
    if (zhashx_size (ctx->active_jobs) == 0)
        drain_empty_notify (ctx->drain);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
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
