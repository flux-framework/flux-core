/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef _FLUX_JOB_MANAGER_JOB_H
#define _FLUX_JOB_MANAGER_JOB_H

#include <stdint.h>
#include <jansson.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libjob/job.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libflux/plugin.h"
#include "ccan/bitmap/bitmap.h"

struct job {
    flux_jobid_t id;
    uint32_t userid;
    int urgency;
    int64_t priority;
    double t_submit;
    int flags;
    json_t *jobspec_redacted;
    int eventlog_seq;           // eventlog count / sequence number
    flux_job_state_t state;
    json_t *end_event;      // event that caused transition to CLEANUP state
    const flux_msg_t *waiter; // flux_job_wait() request
    double t_clean;

    uint8_t depend_posted:1;// depend event already posted
    uint8_t alloc_queued:1; // queued for alloc, but alloc request not sent
    uint8_t alloc_pending:1;// alloc request sent to sched
    uint8_t alloc_bypass:1; // alloc bypass enabled
    uint8_t free_pending:1; // free request sent to sched
    uint8_t has_resources:1;
    uint8_t start_pending:1;// start request sent to job-exec
    uint8_t reattach:1;

    uint8_t perilog_active; // if nonzero, prolog/epilog active

    json_t *annotations;

    struct grudgeset *dependencies;

    zlistx_t *subscribers;  // list of plugins subscribed to all job events

    struct bitmap *events;  // set of events by id posted to this job

    void *handle;           // zlistx_t handle
    int refcount;           // private to job.c

    struct aux_item *aux;
};

void job_decref (struct job *job);
struct job *job_incref (struct job *job);

struct job *job_create (void);

struct job *job_create_from_eventlog (flux_jobid_t id,
                                      const char *eventlog,
                                      const char *jobspec);

int job_aux_set (struct job *job,
                 const char *name,
                 void *val,
                 flux_free_f destroy);
void *job_aux_get (struct job *job, const char *name);
void job_aux_delete (struct job *job, const void *val);

/* Helpers for maintaining czmq containers of 'struct job'.
 * The comparator sorts by (1) priority, then (2) jobid.
 */
void job_destructor (void **item);
void *job_duplicator (const void *item);
int job_comparator (const void *a1, const void *a2);

/*  Add and remove job dependencies
 */
int job_dependency_add (struct job *job, const char *description);
int job_dependency_remove (struct job *job, const char *description);
int job_dependency_count (struct job *job);

/*  Test if dependency event 'event' (dependency-add or dependency-remove)
 *    is valid for this job.
 *  Returns false if:
 *   - EEXIST - when adding a dependency if dependency has already been used
 *   - ENOENT - when removing a dependency that does not exist
 *   - EINVAL - event name is not dependency-add or -remove
 */
bool job_dependency_event_valid (struct job *job,
                                 const char *event,
                                 const char *description);


/*  Set a limited set of flags by name on job
 */
int job_flag_set (struct job *job, const char *flag);

/*  Test if flag name 'flag' is a valid job flag
 */
bool job_flag_valid (struct job *job, const char *flag);

/*  Allow a flux_plugin_t to subscribe to all job events
 */
int job_events_subscribe (struct job *job, flux_plugin_t *p);

void job_events_unsubscribe (struct job *job, flux_plugin_t *p);

/*  Add and test for events posted to jobs by a global event id.
 *  (Event names are translated to id by the event class)
 */
int job_event_id_set (struct job *job, int id);
int job_event_id_test (struct job *job, int id);

#endif /* _FLUX_JOB_MANAGER_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

