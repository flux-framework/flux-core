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
#include <flux/core.h>

#include "job.h"
#include "event.h"

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

struct job *job_create (void)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->refcount = 1;
    job->userid = FLUX_USERID_UNKNOWN;
    job->priority = FLUX_JOB_PRIORITY_DEFAULT;
    job->state = FLUX_JOB_NEW;
    return job;
}

struct job *job_create_from_eventlog (flux_jobid_t id, const char *s)
{
    struct flux_kvs_eventlog *eventlog;
    const char *event;
    struct job *job;

    if (!(job = job_create ()))
        return NULL;
    job->id = id;
    if (!(eventlog = flux_kvs_eventlog_decode (s)))
        goto error;
    event = flux_kvs_eventlog_first (eventlog);
    while (event) {
        if (event_job_update (job, event) < 0)
            goto error;
        event = flux_kvs_eventlog_next (eventlog);
    }
    if (job->state == FLUX_JOB_NEW)
        goto inval;
    flux_kvs_eventlog_destroy (eventlog);
    return job;
inval:
    errno = EINVAL;
error:
    job_decref (job);
    flux_kvs_eventlog_destroy (eventlog);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

