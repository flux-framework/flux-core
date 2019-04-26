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

#include "src/common/libjob/job_hash.h"

#include "job.h"
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
#include "wait.h"
#include "simulator.h"

#include "job-manager.h"

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.list",
        list_handle_request,
        0
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

    if (!(ctx.active_jobs = job_hash_create ())) {
        flux_log_error (h, "error creating active_jobs hash");
        goto done;
    }
    zhashx_set_destructor (ctx.active_jobs, job_destructor);
    zhashx_set_duplicator (ctx.active_jobs, job_duplicator);
    if (!(ctx.event = event_ctx_create (&ctx))) {
        flux_log_error (h, "error creating event batcher");
        goto done;
    }
    if (!(ctx.submit = submit_ctx_create (&ctx))) {
        flux_log_error (h, "error creating submit interface");
        goto done;
    }
    if (!(ctx.alloc = alloc_ctx_create (&ctx))) {
        flux_log_error (h, "error creating scheduler interface");
        goto done;
    }
    if (!(ctx.start = start_ctx_create (&ctx))) {
        flux_log_error (h, "error creating exec interface");
        goto done;
    }
    if (!(ctx.drain = drain_ctx_create (&ctx))) {
        flux_log_error (h, "error creating drain interface");
        goto done;
    }
    if (!(ctx.wait = wait_ctx_create (&ctx))) {
        flux_log_error (h, "error creating wait interface");
        goto done;
    }
    if (!(ctx.simulator = sim_ctx_create (&ctx))) {
        flux_log_error (h, "error creating simulator context");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (restart_from_kvs (&ctx) < 0) {
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
    sim_ctx_destroy (ctx.simulator);
    wait_ctx_destroy (ctx.wait);
    drain_ctx_destroy (ctx.drain);
    start_ctx_destroy (ctx.start);
    alloc_ctx_destroy (ctx.alloc);
    submit_ctx_destroy (ctx.submit);
    event_ctx_destroy (ctx.event);
    zhashx_destroy (&ctx.active_jobs);
    return rc;
}

MOD_NAME ("job-manager");

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
