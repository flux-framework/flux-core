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
#include <czmq.h>
#include <jansson.h>
#include "src/common/libjob/job.h"

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

    uint8_t alloc_queued:1; // queued for alloc, but alloc request not sent
    uint8_t alloc_pending:1;// alloc request sent to sched
    uint8_t free_pending:1; // free request sent to sched
    uint8_t has_resources:1;
    uint8_t start_pending:1;// start request sent to job-exec

    json_t *annotations;

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

#endif /* _FLUX_JOB_MANAGER_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

