/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_JOB_STATE_H
#define _FLUX_JOB_LIST_JOB_STATE_H

#include <flux/core.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "idsync.h"
#include "stats.h"

/* To handle the common case of user queries on job state, we will
 * store jobs in three different lists.
 *
 * - pending - these are jobs that have not yet in the RUN state, they
 *   are sorted based on job urgency (highest first), then job
 *   submission time (earlier submission time first).
 * - running - these are jobs that have transitioned to the RUN state.
 *   They are sorted by initial run start time (later run start
 *   times first).
 * - inactive - these are jobs that are in the INACTIVE state, they
 *   are sorted by job completion time (later completion times
 *   first)
 *
 * There is also an additional list `processing` that stores jobs that
 * cannot yet be stored on one of the lists above.
 *
 * The list `futures` is used to store in process futures.
 */

struct job_state_ctx {
    flux_t *h;
    struct list_ctx *ctx;

    zhashx_t *index;
    zlistx_t *pending;
    zlistx_t *running;
    zlistx_t *inactive;
    zlistx_t *processing;
    zlistx_t *futures;

    /*  Job statistics: */
    struct job_stats_ctx *statsctx;

    /* debug/testing - journal responses queued during pause */
    bool pause;
    struct flux_msglist *backlog;

    /* stream of job events from the job-manager */
    flux_future_t *events;

    bool initialized;
};

struct job_state_ctx *job_state_create (struct list_ctx *ctx);

void job_state_destroy (void *data);

void job_state_pause_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg);

void job_state_unpause_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg);

int job_state_config_reload (struct job_state_ctx *jsctx,
                             const flux_conf_t *conf,
                             flux_error_t *errp);

#endif /* ! _FLUX_JOB_LIST_JOB_STATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
