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
#include "src/common/libjob/job.h"

struct job {
    flux_jobid_t id;
    uint32_t userid;
    int priority;
    double t_submit;
    int flags;
    flux_job_state_t state;

    uint8_t alloc_pending:1;
    uint8_t free_pending:1;
    uint8_t has_resources:1;

    void *aux_queue_handle;
    void *queue_handle; // primary queue handle (for listing all active jobs)
    int refcount;       // private to job.c
};

void job_decref (struct job *job);
struct job *job_incref (struct job *job);

struct job *job_create (void);

/* (re-)create job by replaying its KVS eventlog.
 */
struct job *job_create_from_eventlog (flux_jobid_t id, const char *eventlog);

#endif /* _FLUX_JOB_MANAGER_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

