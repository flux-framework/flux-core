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
#include "util.h"

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

static struct job *job_create_uninit (flux_jobid_t id)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    job->refcount = 1;
    job->id = id;
    job->userid = FLUX_USERID_UNKNOWN;
    job->priority = FLUX_JOB_PRIORITY_DEFAULT;
    job->state = FLUX_JOB_NEW;
    return job;
}

struct job *job_create (flux_jobid_t id, int priority, uint32_t userid,
                        double t_submit, int flags)
{
    struct job *job;

    if (!(job = job_create_uninit (id)))
        return NULL;
    job->userid = userid;
    job->priority = priority;
    job->t_submit = t_submit;
    job->flags = flags;
    job->state = FLUX_JOB_NEW;
    return job;
}

struct job *job_create_from_eventlog (flux_jobid_t id, const char *s)
{
    struct flux_kvs_eventlog *eventlog;
    const char *event;
    double timestamp;
    char name[FLUX_KVS_MAX_EVENT_NAME + 1];
    char context[FLUX_KVS_MAX_EVENT_CONTEXT + 1];
    bool submit_valid = false;
    struct job *job;

    if (!(job = job_create_uninit (id)))
        return NULL;
    if (!(eventlog = flux_kvs_eventlog_decode (s)))
        goto error;
    event = flux_kvs_eventlog_first (eventlog);
    while (event) {
        if (flux_kvs_event_decode (event, &timestamp,
                                   name, sizeof (name),
                                   context, sizeof (context)) < 0)
            goto error;
        if (!strcmp (name, "submit")) {
            int priority, userid;
            if (util_int_from_context (context, "priority", &priority) < 0)
                goto error;
            if (util_int_from_context (context, "userid", &userid) < 0)
                goto error;
            job->t_submit = timestamp;
            job->userid = userid;
            job->priority = priority;
            submit_valid = true;
        }
        else if (!strcmp (name, "priority")) {
            int priority;
            if (util_int_from_context (context, "priority", &priority) < 0)
                goto error;
            job->priority = priority;
        }
        else if (!strcmp (name, "exception")) {
            int severity;
            if (util_int_from_context (context, "severity", &severity) < 0)
                goto error;
            if (severity == 0)
                job->state = FLUX_JOB_CLEANUP;
        }
        event = flux_kvs_eventlog_next (eventlog);
    }
    if (!submit_valid) {
        errno = EINVAL;
        goto error;
    }
    flux_kvs_eventlog_destroy (eventlog);
    return job;
error:
    job_decref (job);
    flux_kvs_eventlog_destroy (eventlog);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

