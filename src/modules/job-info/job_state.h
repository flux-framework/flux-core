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
#include <jansson.h>

#include "info.h"

/* To handle the common case of user queries on job state, we will
 * store jobs in three different lists.
 *
 * - pending - these are jobs that have not yet in the RUN state, they
 *   are sorted based on job priority (highest first), then job
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
    struct info_ctx *ctx;
    zhashx_t *index;
    zlistx_t *pending;
    zlistx_t *running;
    zlistx_t *inactive;
    zlistx_t *processing;
    zlistx_t *futures;

    /* count current jobs in what states */
    int depend_count;
    int sched_count;
    int run_count;
    int cleanup_count;
    int inactive_count;

    /* annotations that arrived before job is known */
    zhashx_t *early_annotations;

    /* debug/testing - if paused store job transitions on list for
     * processing later */
    bool pause;
    zlistx_t *transitions;

    /* stream of job events from the job-manager */
    flux_future_t *events;
};

struct job {
    struct info_ctx *ctx;

    flux_jobid_t id;
    uint32_t userid;
    int priority;
    double priority_timestamp;
    double t_submit;
    int flags;
    flux_job_state_t state;
    const char *name;
    int ntasks;
    int nnodes;
    char *ranks;
    double expiration;
    bool success;
    bool exception_occurred;
    json_t *exception_context;
    int exception_severity;
    const char *exception_type;
    const char *exception_note;
    flux_job_result_t result;
    double annotations_timestamp;
    json_t *annotations;

    /* cache of job information */
    json_t *jobspec_job;
    json_t *jobspec_cmd;
    json_t *R;

    /* Track which states we have seen and have completed transition
     * to.  We do not immediately update to the new state and place
     * onto a new list until we have retrieved any necessary data
     * associated to that state.  For example, when the 'depend' state
     * has been seen, we don't immediately place it on the `pending`
     * list.  We wait until we've retrieved data such as userid,
     * priority, etc.
     *
     * Track which states we've seen via the states_mask.
     */
    zlist_t *next_states;
    unsigned int states_mask;
    void *list_handle;

    /* timestamp of when we enter the state
     *
     * associated eventlog entries when restarting
     *
     * depend - "submit"
     * sched - "depend"
     * run - "alloc"
     * cleanup - "finish" or "exception" w/ severity == 0
     * inactive - "clean"
     */
    // t_depend is identical to t_submit above, use that
    // double t_depend;
    double t_sched;
    double t_run;
    double t_cleanup;
    double t_inactive;
};

struct job_state_ctx *job_state_create (struct info_ctx *ctx);

void job_state_destroy (void *data);

void job_state_cb (flux_t *h, flux_msg_handler_t *mh,
                   const flux_msg_t *msg, void *arg);

void job_state_pause_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg);

void job_state_unpause_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg);

int job_state_init_from_kvs (struct info_ctx *ctx);

#endif /* ! _FLUX_JOB_INFO_JOB_STATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
