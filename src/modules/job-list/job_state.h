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

#include "job-list.h"
#include "stats.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

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
    struct job_stats stats;

    /* debug/testing - journal responses queued during pause */
    bool pause;
    struct flux_msglist *backlog;

    /* stream of job events from the job-manager */
    flux_future_t *events;
};

/* timestamp of when we enter the state
 *
 * associated eventlog entries when restarting
 *
 * t_depend - "submit"
 * t_priority - "priority" (not saved, can be entered multiple times)
 * t_sched - "depend" (not saved, can be entered multiple times)
 * t_run - "alloc"
 * t_cleanup - "finish" or "exception" w/ severity == 0
 * t_inactive - "clean"
 */
struct job {
    struct list_ctx *ctx;

    flux_jobid_t id;
    uint32_t userid;
    int urgency;
    int64_t priority;
    double t_submit;
    // t_depend is identical to t_submit
    // double t_depend;
    double t_run;
    double t_cleanup;
    double t_inactive;
    flux_job_state_t state;
    const char *name;
    int ntasks;
    int nnodes;
    char *ranks;
    char *nodelist;
    double expiration;
    int wait_status;
    bool success;
    bool exception_occurred;
    int exception_severity;
    const char *exception_type;
    const char *exception_note;
    flux_job_result_t result;
    json_t *annotations;
    struct grudgeset *dependencies;

    /* cache of job information */
    json_t *jobspec;
    json_t *R;
    json_t *exception_context;

    /* Track which states we have seen and have completed transition
     * to.  We do not immediately update to the new state and place
     * onto a new list until we have retrieved any necessary data
     * associated to that state.  For example, when the 'depend' state
     * has been seen, we don't immediately place it on the `pending`
     * list.  We wait until we've retrieved data such as userid,
     * urgency, etc.
     *
     * Track which states we've seen via the states_mask.
     *
     * Track states seen via events stream in states_events_mask.
     */
    zlist_t *next_states;
    unsigned int states_mask;
    unsigned int states_events_mask;
    void *list_handle;

    int eventlog_seq;           /* last event seq read */
};

struct job_state_ctx *job_state_create (struct list_ctx *ctx);

void job_state_destroy (void *data);

void job_state_pause_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg);

void job_state_unpause_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg);

int job_state_init_from_kvs (struct list_ctx *ctx);

#endif /* ! _FLUX_JOB_LIST_JOB_STATE_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
