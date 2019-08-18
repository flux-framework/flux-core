/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* simulator.c - sim interface
 *
 * This interface is primarily built so that the simulator can determine when
 * the system has become quiescent (assuming no further events from external
 * sources). Before responding to a quiescent request, the job-manager will
 * ensure that all of the relevant modules (e.g., sched, exec, and depend) are
 * also quiescent.
 */
#include <flux/core.h>
#include <stdbool.h>

#include "simulator.h"

struct simulator {
    struct job_manager* ctx;
    flux_msg_handler_t **handlers;
    flux_msg_t *sim_req;
    flux_future_t *sched_req;
    int num_outstanding_job_starts;
};

void sim_ctx_destroy (struct simulator *ctx)
{
    if (ctx) {
        int saved_errno = errno;;
        flux_msg_handler_delvec (ctx->handlers);
        if (ctx->sim_req)
            flux_msg_destroy(ctx->sim_req);
        free (ctx);
        errno = saved_errno;
    }
}

static inline bool is_quiescent (struct simulator *simulator)
{
    return (simulator->sched_req == NULL) && (simulator->num_outstanding_job_starts == 0);
}

static void check_and_respond_to_quiescent_req (struct simulator *simulator)
{
    if (simulator->sim_req == NULL || !is_quiescent (simulator)) {
        // Either not in a simulation or not quiesced
        return;
    }

    const char *sim_payload = NULL;
    flux_msg_get_string (simulator->sim_req, &sim_payload);
    flux_log (simulator->ctx->h,
              LOG_DEBUG,
              "replying to sim quiescent req with (%s)",
              sim_payload);
    flux_respond (simulator->ctx->h, simulator->sim_req, sim_payload);
    flux_msg_destroy (simulator->sim_req);
    simulator->sim_req = NULL;
}

static void sched_quiescent_continuation(flux_future_t *f, void *arg)
{
    struct job_manager *ctx = arg;
    struct simulator *simulator = ctx->simulator;

    if (simulator->sim_req == NULL) {
        flux_log_error (ctx->h, "%s: sim quiescent request is NULL", __FUNCTION__);
        return;
    }
    if (simulator->sched_req != f) {
        flux_log_error (ctx->h, "%s: stored future does not match continuation future", __FUNCTION__);
        return;
    }

    flux_log (ctx->h, LOG_DEBUG, "receive quiescent from sched");
    simulator->sched_req = NULL;
    check_and_respond_to_quiescent_req (simulator);
    flux_future_destroy (f);
}

void sim_sending_sched_request (struct simulator *simulator)
{
    struct job_manager *ctx = simulator->ctx;
    if (simulator->sim_req == NULL) {
        // either not in a simulation, or if we are, the simulator does not yet
        // care about tracking quiescence
        return;
    }
    if (simulator->sched_req != NULL) {
        // we are sending the scheduler more work/events before hearing back
        // from the previous quiescent request, destroy the future from that
        // previous request before sending a new request
        flux_future_destroy (simulator->sched_req);
    }

    flux_log (ctx->h, LOG_DEBUG, "sending quiescent req to scheduler");
    simulator->sched_req = flux_rpc (ctx->h, "sched.quiescent", NULL, 0, 0);
    if (simulator->sched_req == NULL)
        flux_respond_error(ctx->h, simulator->sim_req, errno, "job-manager: sim_sending_sched_request: flux_rpc failed");
    if (flux_future_then (simulator->sched_req, -1, sched_quiescent_continuation, ctx) < 0)
        flux_respond_error(ctx->h, simulator->sim_req, errno, "job-manager: sim_sending_sched_request: flux_future_then failed");
}

void sim_received_alloc_response (struct simulator *simulator)
{
    simulator->num_outstanding_job_starts++;
    flux_log (simulator->ctx->h,
              LOG_DEBUG,
              "received an alloc response, outstanding job starts == %d",
              simulator->num_outstanding_job_starts);
}

void sim_received_start_response (struct simulator *simulator)
{
    simulator->num_outstanding_job_starts--;
    flux_log (simulator->ctx->h,
              LOG_DEBUG,
              "received a start response, outstanding job starts == %d",
              simulator->num_outstanding_job_starts);
    check_and_respond_to_quiescent_req (simulator);
}

/* Handle a job-manager.quiescent request.  We'll first copy the request into
 * ctx for later response, and then kick off the process of verifying that all
 * relevant modules are quiesced.
 */
static void quiescent_cb (flux_t *h, flux_msg_handler_t *mh,
                          const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    struct simulator *simulator = ctx->simulator;
    flux_log (ctx->h, LOG_DEBUG, "received quiescent request");
    simulator->sim_req = flux_msg_copy (msg, true);
    if (simulator->sim_req == NULL)
        flux_respond_error(h, msg, errno, "job-manager: quiescent_cb: flux_msg_copy failed");

    // Check if the scheduler is quiesced
    sim_sending_sched_request(simulator);
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.quiescent", quiescent_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct simulator *sim_ctx_create (struct job_manager *ctx)
{
    struct simulator *simulator;

    if (!(simulator = calloc (1, sizeof (*simulator))))
        return NULL;
    simulator->ctx = ctx;
    simulator->sim_req = NULL;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &simulator->handlers) < 0)
        goto error;
    return simulator;
error:
    sim_ctx_destroy (simulator);
    return NULL;
}
