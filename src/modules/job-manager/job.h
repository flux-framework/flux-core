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

#include "src/common/libjob/job.h"

enum job_status_flags {
    JOB_EXCEPTION_PENDING = 1,  // exception not yet commited to eventlog
};

struct job {
    flux_jobid_t id;
    uint32_t userid;
    int priority;
    double t_submit;
    int flags;
    flux_job_state_t state;

    void *list_handle;  // private to queue.c
    int refcount;       // private to job.c
};

void job_decref (struct job *job);
struct job *job_incref (struct job *job);

struct job *job_create (flux_jobid_t id,
                        int priority,
                        uint32_t userid,
                        double t_submit,
                        int flags);

/* (re-)create job by replaying its KVS eventlog.
 */
struct job *job_create_from_eventlog (flux_jobid_t id, const char *eventlog);

#endif /* _FLUX_JOB_MANAGER_JOB_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

