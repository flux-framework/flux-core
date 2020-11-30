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
#include <jansson.h>
#include <czmq.h>

#include "job.h"
#include "event.h"

#include "src/common/libeventlog/eventlog.h"

void job_decref (struct job *job)
{
    if (job && --job->refcount == 0) {
        int saved_errno = errno;
        json_decref (job->end_event);
        flux_msg_decref (job->waiter);
        json_decref (job->annotations);
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
    job->admin_priority = FLUX_JOB_ADMIN_PRIORITY_DEFAULT;
    job->queue_priority = FLUX_JOB_QUEUE_PRIORITY_DEFAULT;
    job->state = FLUX_JOB_STATE_NEW;
    return job;
}

struct job *job_create_from_eventlog (flux_jobid_t id, const char *s)
{
    struct job *job;
    json_t *a = NULL;
    size_t index;
    json_t *event;

    if (!(job = job_create ()))
        return NULL;
    job->id = id;

    if (!(a = eventlog_decode (s)))
        goto error;

    json_array_foreach (a, index, event) {
        if (event_job_update (job, event) < 0)
            goto error;
        job->eventlog_seq++;
    }

    if (job->state == FLUX_JOB_STATE_NEW)
        goto inval;

    json_decref (a);
    return job;
inval:
    errno = EINVAL;
error:
    job_decref (job);
    json_decref (a);
    return NULL;
}

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* Decref a job.
 * N.B. zhashx_destructor_fn / zlistx_destructor_fn signature
 */
void job_destructor (void **item)
{
    if (item) {
        job_decref (*item);
        *item = NULL;
    }
}

/* Duplicate a job
 * N.B. zhashx_duplicator_fn / zlistx_duplicator_fn signature
 */
void *job_duplicator (const void *item)
{
    return job_incref ((struct job *)item);
}

/* Compare jobs, ordering by (1) priority, (2) job id.
 * N.B. zlistx_comparator_fn signature
 */
int job_comparator (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->queue_priority, j2->queue_priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

