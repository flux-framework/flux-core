/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_LIST_JOB_DATA_H
#define _FLUX_JOB_LIST_JOB_DATA_H

#include <flux/core.h>
#include <jansson.h>

#include "src/common/libhostlist/hostlist.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libczmqcontainers/czmq_containers.h"

/* timestamp of when we enter the state
 *
 * associated eventlog entries when restarting
 *
 * t_submit = "submit"
 * t_depend - "validate"
 * t_priority - "priority" (not saved, can be entered multiple times)
 * t_sched - "depend" (not saved, can be entered multiple times)
 * t_run - "alloc"
 * t_cleanup - "finish" or "exception" w/ severity == 0
 * t_inactive - "clean"
 */
struct job {
    flux_t *h;

    flux_jobid_t id;
    uint32_t userid;
    int urgency;
    int64_t priority;
    double t_submit;
    double t_depend;
    double t_run;
    double t_cleanup;
    double t_inactive;
    flux_job_state_t state;
    const char *name;
    const char *queue;
    const char *cwd;
    const char *project;
    const char *bank;
    int ntasks;
    int ntasks_per_core_on_node_count;  /* flag for ntasks calculation */
    int ncores;
    double duration;
    int nnodes;
    char *ranks;
    char *nodelist;
    struct hostlist *nodelist_hl; /* cache of nodelist in hl form */
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
     * to.  States we've processed via the states_mask and states seen
     * via events stream in states_events_mask.
     */
    unsigned int states_mask;
    unsigned int states_events_mask;
    void *list_handle;

    int submit_version;         /* version number in submit context */
};

void job_destroy (void *data);

struct job *job_create (flux_t *h, flux_jobid_t id);

/* Parse and internally cache jobspec.  Set values for:
 * - job name
 * - queue
 * - ntasks
 * - nnodes (if available)
 * - ncores (if possible)
 * - duration
 *
 * Optionally pass in "updates", an object with path:value updates to
 * the jobspec.
 */
int job_parse_jobspec (struct job *job, const char *s, json_t *updates);
int job_parse_jobspec_cached (struct job *job, json_t *updates);

/* identical to above, but all nonfatal errors will return error.
 * Primarily used for testing.
 */
int job_parse_jobspec_fatal (struct job *job, const char *s, json_t *updates);

/* Update jobspec with period delimited paths
 * (i.e. "attributes.system.duration") and value.
 */
int job_jobspec_update (struct job *job, json_t *updates);

/* Parse and internally cache R.  Set values for:
 * - expiration
 * - nnodes
 * - nodelist
 * - ncores
 * - ntasks (if necessary)
 */
int job_parse_R (struct job *job, const char *s, json_t *updates);
int job_parse_R_cached (struct job *job, json_t *updates);

/* identical to above, but all nonfatal errors will return error.
 * Primarily used for testing.
 */
int job_parse_R_fatal (struct job *job, const char *s, json_t *updates);

/* Update R with RFC21 defined keys
 * (i.e. "expiration") and value.
 */
int job_R_update (struct job *job, json_t *updates);

#endif /* ! _FLUX_JOB_LIST_JOB_DATA_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
