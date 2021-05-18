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
#include <assert.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libutil/aux.h"

#include "job.h"
#include "event.h"

void job_decref (struct job *job)
{
    if (job && --job->refcount == 0) {
        int saved_errno = errno;
        json_decref (job->end_event);
        flux_msg_decref (job->waiter);
        json_decref (job->jobspec_redacted);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        aux_destroy (&job->aux);
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
    job->urgency = FLUX_JOB_URGENCY_DEFAULT;
    job->priority = -1;
    job->state = FLUX_JOB_STATE_NEW;
    return job;
}

int job_dependency_count (struct job *job)
{
    return grudgeset_size (job->dependencies);
}

int job_dependency_add (struct job *job, const char *description)
{
    assert (job->state == FLUX_JOB_STATE_DEPEND);
    if (grudgeset_add (&job->dependencies, description) < 0
        && errno != EEXIST)
        return -1;
    return job_dependency_count (job);
}

int job_dependency_remove (struct job *job, const char *description)
{
    return grudgeset_remove (job->dependencies, description);
}

bool job_dependency_event_valid (struct job *job,
                                 const char *event,
                                 const char *description)
{
    if (strcmp (event, "dependency-add") == 0) {
        if (grudgeset_used (job->dependencies, description)) {
            errno = EEXIST;
            return false;
        }
    }
    else if (strcmp (event, "dependency-remove") == 0) {
        if (!grudgeset_contains (job->dependencies, description)) {
            errno = ENOENT;
            return false;
        }
    }
    else {
        errno = EINVAL;
        return false;
    }
    return true;
}

int job_aux_set (struct job *job,
                 const char *name,
                 void *val,
                 flux_free_f destroy)
{
    return aux_set (&job->aux, name, val, destroy);
}

void *job_aux_get (struct job *job, const char *name)
{
    return aux_get (job->aux, name);
}

void job_aux_delete (struct job *job, const void *val)
{
    aux_delete (&job->aux, val);
}

/* Follow path (NULL terminated array of keys) through multiple JSON
 * object levels, and delete the final path component.
 */
void delete_json_path (json_t *o, const char *path[])
{
    if (o && path && path[0]) {
        if (path[1])
            delete_json_path (json_object_get (o, path[0]), &path[1]);
        else
            json_object_del (o, path[0]);
    }
}

struct job *job_create_from_eventlog (flux_jobid_t id,
                                      const char *eventlog,
                                      const char *jobspec)
{
    struct job *job;
    json_t *a = NULL;
    size_t index;
    json_t *event;
    const char *envpath[] = { "attributes", "system", "environment", NULL };

    if (!(job = job_create ()))
        return NULL;
    job->id = id;

    if (!(job->jobspec_redacted = json_loads (jobspec, 0, NULL)))
        goto inval;
    delete_json_path (job->jobspec_redacted, envpath);

    if (!(a = eventlog_decode (eventlog)))
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

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

