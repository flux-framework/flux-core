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
#include <unistd.h>
#include <sys/types.h>
#include <flux/core.h>

#include "src/common/libjob/job_hash.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "conf.h"
#include "submit.h"
#include "restart.h"
#include "raise.h"
#include "kill.h"
#include "list.h"
#include "urgency.h"
#include "alloc.h"
#include "housekeeping.h"
#include "start.h"
#include "event.h"
#include "drain.h"
#include "wait.h"
#include "purge.h"
#include "queue.h"
#include "annotate.h"
#include "journal.h"
#include "getattr.h"
#include "update.h"
#include "jobtap-internal.h"

#include "job-manager.h"

void getinfo_handle_request (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct job_manager *ctx = arg;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:I}",
                           "max_jobid", ctx->max_jobid) < 0)
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void disconnect_rpc (flux_t *h,
                     flux_msg_handler_t *mh,
                     const flux_msg_t *msg,
                     void *arg)
{
    /* disconnects occur once per client, there is no way to know
     * which services a client used, so we must check all services for
     * cleanup */
    alloc_disconnect_rpc (h, mh, msg, arg);
    wait_disconnect_rpc (h, mh, msg, arg);
    journal_listeners_disconnect_rpc (h, mh, msg, arg);
}

static void stats_cb (flux_t *h, flux_msg_handler_t *mh,
                      const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    json_t *journal = journal_get_stats (ctx->journal);
    json_t *housekeeping = housekeeping_get_stats (ctx->housekeeping);
    if (!housekeeping || !journal)
        goto error;
    if (flux_respond_pack (h,
                           msg,
                           "{s:O s:i s:i s:I s:O}",
                           "journal", journal,
                           "active_jobs", zhashx_size (ctx->active_jobs),
                           "inactive_jobs", zhashx_size (ctx->inactive_jobs),
                           "max_jobid", ctx->max_jobid,
                           "housekeeping", housekeeping) < 0) {
        flux_log_error (h, "%s: flux_respond_pack", __FUNCTION__);
        goto error;
    }
    json_decref (housekeeping);
    json_decref (journal);
    return;
 error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    json_decref (housekeeping);
    json_decref (journal);
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.list",
        list_handle_request,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.urgency",
        urgency_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.getattr",
        getattr_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.getinfo",
        getinfo_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.jobtap",
        jobtap_handler,
        FLUX_ROLE_OWNER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.jobtap-query",
        jobtap_query_handler,
        FLUX_ROLE_OWNER,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.disconnect",
        disconnect_rpc,
        0
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.stats-get",
        stats_cb,
        FLUX_ROLE_USER,
    },

    FLUX_MSGHANDLER_TABLE_END,
};

int mod_main (flux_t *h, int argc, char **argv)
{
    flux_reactor_t *r = flux_get_reactor (h);
    int rc = -1;
    struct job_manager ctx;
    flux_error_t error;

    memset (&ctx, 0, sizeof (ctx));
    ctx.h = h;
    ctx.owner = getuid ();

    if (!(ctx.active_jobs = job_hash_create ())
        || !(ctx.inactive_jobs = job_hash_create ())) {
        flux_log_error (h, "error creating jobs hash");
        goto done;
    }
    zhashx_set_destructor (ctx.active_jobs, job_destructor);
    zhashx_set_duplicator (ctx.active_jobs, job_duplicator);
    zhashx_set_destructor (ctx.inactive_jobs, job_destructor);
    zhashx_set_duplicator (ctx.inactive_jobs, job_duplicator);
    if (!(ctx.conf = conf_create (&ctx, &error))) {
        flux_log (h, LOG_ERR, "config: %s", error.text);
        goto done;
    }
    if (!(ctx.jobtap = jobtap_create (&ctx))) {
        flux_log (h, LOG_ERR, "error creating jobtap interface");
        goto done;
    }
    if (!(ctx.purge = purge_create (&ctx))) {
        flux_log_error (h, "error creating purge context");
        goto done;
    }
    if (!(ctx.queue = queue_create (&ctx))) {
        flux_log_error (h, "error creating queue context");
        goto done;
    }
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
    if (!(ctx.housekeeping = housekeeping_ctx_create (&ctx))) {
        flux_log_error (h, "error creating resource housekeeping interface");
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
    if (!(ctx.raise = raise_ctx_create (&ctx))) {
        flux_log_error (h, "error creating raise interface");
        goto done;
    }
    if (!(ctx.kill = kill_ctx_create (&ctx))) {
        flux_log_error (h, "error creating kill interface");
        goto done;
    }
    if (!(ctx.annotate = annotate_ctx_create (&ctx))) {
        flux_log_error (h, "error creating annotate interface");
        goto done;
    }
    if (!(ctx.journal = journal_ctx_create (&ctx))) {
        flux_log_error (h, "error creating journal interface");
        goto done;
    }
    if (!(ctx.update = update_ctx_create (&ctx))) {
        flux_log_error (h, "error creating job update interface");
        goto done;
    }
    if (flux_msg_handler_addvec (h, htab, &ctx, &ctx.handlers) < 0) {
        flux_log_error (h, "flux_msghandler_add");
        goto done;
    }
    if (restart_from_kvs (&ctx) < 0) // logs its own error messages
        goto done;
    if (flux_reactor_run (r, 0) < 0) {
        flux_log_error (h, "flux_reactor_run");
        goto done;
    }
    if (restart_save_state (&ctx) < 0) {
        flux_log_error (h, "error saving job manager state to KVS");
        goto done;
    }
    rc = 0;
done:
    flux_msg_handler_delvec (ctx.handlers);
    queue_destroy (ctx.queue);
    purge_destroy (ctx.purge);
    journal_ctx_destroy (ctx.journal);
    annotate_ctx_destroy (ctx.annotate);
    kill_ctx_destroy (ctx.kill);
    raise_ctx_destroy (ctx.raise);
    wait_ctx_destroy (ctx.wait);
    drain_ctx_destroy (ctx.drain);
    start_ctx_destroy (ctx.start);
    housekeeping_ctx_destroy (ctx.housekeeping);
    alloc_ctx_destroy (ctx.alloc);
    submit_ctx_destroy (ctx.submit);
    event_ctx_destroy (ctx.event);
    update_ctx_destroy (ctx.update);
    /* job aux containers may call destructors in jobtap plugins, so destroy
     * jobs before unloading plugins; but don't destroy job hashes until after.
     */
    zhashx_purge (ctx.active_jobs);
    zhashx_purge (ctx.inactive_jobs);
    jobtap_destroy (ctx.jobtap);
    conf_destroy (ctx.conf);
    zhashx_destroy (&ctx.active_jobs);
    zhashx_destroy (&ctx.inactive_jobs);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
