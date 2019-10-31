/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_INFO_JOB_STATE_H
#define _FLUX_JOB_INFO_JOB_STATE_H

#include <flux/core.h>

#include "info.h"

/* To handle the common case of user queries on job state, we will
 * store jobs in three different lists.
 *
 * - pending - these are jobs that have not yet in the RUN state, they
 *   are sorted based on job priority, then job submission time.
 * - running - these are jobs that have transitioned to the RUN state.
 *   They are sorted by initial run start time.
 * - inactive - these are jobs that are in the INACTIVE state, they
 *   are sorted by job completion time.
 *
 * There is also an additional list `processing` that stores jobs that
 * cannot yet be stored on one of the lists above.
 *
 * The list `futures` is used to store in process futures.
 */

struct job_state_ctx {
    flux_t *h;
    zhashx_t *index;
    zlistx_t *pending;
    zlistx_t *running;
    zlistx_t *inactive;
    zlistx_t *processing;
    zlistx_t *futures;
};

struct job {
    struct info_ctx *ctx;

    flux_jobid_t id;
    uint32_t userid;
    int priority;
    double t_submit;
    int flags;
    flux_job_state_t state;

    /* if userid, priority, t_submit, and flags have been set */
    bool job_info_retrieved;
    void *list_handle;
};

struct job_state_ctx *job_state_create (flux_t *h);

void job_state_destroy (void *data);

void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                   const flux_msg_t *msg, void *arg);

#endif /* ! _FLUX_JOB_INFO_JOB_STATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
