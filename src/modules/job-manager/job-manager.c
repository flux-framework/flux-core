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
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "submit.h"
#include "restart.h"
#include "raise.h"
#include "kill.h"
#include "list.h"
#include "priority.h"
#include "alloc.h"
#include "start.h"
#include "event.h"
#include "drain.h"

#include "job-manager.h"

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.list",
        list_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.raise",
        raise_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.kill",
        kill_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.priority",
        priority_handle_request,
        FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_manager ctx;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;

    if (!(ctx.queue = queue_create (true))) {
        flux_log_error (h, "error creating queue");
        goto done;
    }
    if (!(ctx.event_ctx = event_ctx_create (h, ctx.queue))) {
        flux_log_error (h, "error creating event batcher");
        goto done;
    }
    if (!(ctx.submit = submit_ctx_create (&ctx))) {
        flux_log_error (h, "error creating submit interface");
        goto done;
    }
    if (!(ctx.alloc_ctx = alloc_ctx_create (h, ctx.queue, ctx.event_ctx))) {
        flux_log_error (h, "error creating scheduler interface");
        goto done;
    }
    if (!(ctx.start_ctx = start_ctx_create (h, ctx.queue, ctx.event_ctx))) {
        flux_log_error (h, "error creating exec interface");
        goto done;
    }
    if (!(ctx.drain = drain_ctx_create (&ctx))) {
        flux_log_error (h, "error creating drain interface");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (restart_from_kvs (h, ctx.queue, ctx.event_ctx) < 0) {
        flux_log_error (h, "restart_from_kvs");
        goto done;
    }
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    drain_ctx_destroy (ctx.drain);
    start_ctx_destroy (ctx.start_ctx);
    alloc_ctx_destroy (ctx.alloc_ctx);
    submit_ctx_destroy (ctx.submit);
    event_ctx_destroy (ctx.event_ctx);
    queue_destroy (ctx.queue);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
