/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include "src/common/libjob/job.h"
#include "job.h"

void job_decref (struct job *job)
{
    if (job && --job->refcount == 0) {
        int saved_errno = errno;
        free (job);
        errno = saved_errno;
    }
}

struct job *job_incref (struct job *job)
{
    if (!job)
        return NULL;
    job->refcount++;
    return job;
}

struct job *job_create (flux_jobid_t id, int priority, uint32_t userid,
                        double t_submit, int flags)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->refcount = 1;
    job->id = id;
    job->userid = userid;
    job->priority = priority;
    job->t_submit = t_submit;
    job->flags = flags;
    job->state = FLUX_JOB_NEW;
    return job;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

