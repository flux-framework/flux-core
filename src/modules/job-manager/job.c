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
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/aux.h"

#include "job.h"
#include "event.h"

#define EVENTS_BITMAP_SIZE 64

static void subscribers_destroy (struct job *job);

void job_decref (struct job *job)
{
    if (job && --job->refcount == 0) {
        int saved_errno = errno;
        json_decref (job->end_event);
        flux_msg_decref (job->waiter);
        json_decref (job->jobspec_redacted);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        subscribers_destroy (job);
        free (job->events);
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
    if (!(job->events = bitmap_alloc0 (EVENTS_BITMAP_SIZE))) {
        job_decref (job);
        return NULL;
    }
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

static int job_flag_set_internal (struct job *job,
                                  const char *flag,
                                  bool dry_run)
{
   if (strcmp (flag, "alloc-bypass") == 0) {
        if (!dry_run)
            job->alloc_bypass = 1;
    }
    else if (strcmp (flag, "debug") == 0) {
        if (!dry_run)
            job->flags |= FLUX_JOB_DEBUG;
    }
    else {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int job_flag_set (struct job *job, const char *flag)
{
    return job_flag_set_internal (job, flag, false);
}

bool job_flag_valid (struct job *job, const char *flag)
{
    if (job_flag_set_internal (job, flag, true) < 0)
        return false;
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

struct job *job_create_from_eventlog (flux_jobid_t id,
                                      const char *eventlog,
                                      const char *jobspec)
{
    struct job *job;
    json_t *a = NULL;
    size_t index;
    json_t *event;

    if (!(job = job_create ()))
        return NULL;
    job->id = id;

    if (!(job->jobspec_redacted = json_loads (jobspec, 0, NULL)))
        goto inval;
    jpath_del (job->jobspec_redacted, "attributes.system.environment");

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
int job_priority_comparator (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}


/*  This structure is stashed in a plugin which has subscribed to
 *   job events. The reference to the plugin itself is required so
 *   that the aux_item destructor can remove the plugin itself from
 *   the subscribers list of any active jobs to which it had an
 *   active subscription. (See plugin_job_subscriptions_destroy()).
 */
struct plugin_job_subscriptions {
    flux_plugin_t *p;
    zlistx_t *jobs;
};

static struct plugin_job_subscriptions *
plugin_job_subscriptions_create (flux_plugin_t *p)
{
    struct plugin_job_subscriptions *ps = malloc (sizeof (*ps));
    if (!ps || !(ps->jobs = zlistx_new ())) {
        free (ps);
        return NULL;
    }
    ps->p = p;
    return ps;
}

static void plugin_job_subscriptions_destroy (void *arg)
{
    struct plugin_job_subscriptions *ps = arg;
    if (ps) {
        struct job *job = zlistx_first (ps->jobs);
        while (job) {
            job_events_unsubscribe (job, ps->p);
            job = zlistx_next (ps->jobs);
        }
        zlistx_destroy (&ps->jobs);
        free (ps);
    }
}

/*  Clear the subscription of a plugin from a job
 */
static void plugin_clear_subscription (flux_plugin_t *p, struct job *job)
{
    struct plugin_job_subscriptions *ps;
    if ((ps = flux_plugin_aux_get (p, "flux::job-subscriptions"))) {
        void *handle = zlistx_find (ps->jobs, job);
        if (handle)
            zlistx_delete (ps->jobs, handle);
    }
}

/*  Unsubscribe plugin p from job events.
 *
 *  Clear plugin p from this job's list of subscribers as well as
 *   the job from the plugin's list of subscriptions.
 */
void job_events_unsubscribe (struct job *job, flux_plugin_t *p)
{
    void *handle;
    if (job->subscribers
        && (handle = zlistx_find (job->subscribers, p))) {

        /*  Remove plugin from job */
        zlistx_delete (job->subscribers, handle);

        /*  Remove job from plugin */
        plugin_clear_subscription (p, job);
    }
}

/*  Destroy this job's plugin subscribers list.
 *
 *  This can't be done with a zlist destructor because we need
 *   a reference to the struct job during destruction in order to remove
 *   the job from all subscribed plugins subscription lists.
 *
 */
static void subscribers_destroy (struct job *job)
{
    if (job->subscribers) {
        flux_plugin_t *p = zlistx_first (job->subscribers);
        while (p) {
            plugin_clear_subscription (p, job);
            p = zlistx_next (job->subscribers);
        }
    }
    zlistx_destroy (&job->subscribers);
}

/*  Add a plugin to a job's subscribers list.
 */
int job_events_subscribe (struct job *job, flux_plugin_t *p)
{
    struct plugin_job_subscriptions *ps;

    /*  Create a subscribers list for the job if it does not already exist
     */
    if (!job->subscribers && !(job->subscribers = zlistx_new())) {
        errno = ENOMEM;
        return -1;
    }

    /*  Create a subscriptions list for the plugin if it does not already
     *   exist. Ensure that when the plugin is unloaded it is unsubscribed
     *   from all current job events subscriptions.
     */
    if (!(ps = flux_plugin_aux_get (p, "flux::job-subscriptions"))) {
        if (!(ps = plugin_job_subscriptions_create (p))
            || flux_plugin_aux_set (p,
                                    "flux::job-subscriptions",
                                    ps,
                                    plugin_job_subscriptions_destroy) < 0) {
            plugin_job_subscriptions_destroy (ps);
            errno = ENOMEM;
            return -1;
        }
    }

    /*  Add the job to the plugin subscriptions list, and the plugin to the
     *   the job's subscribers list. If either fails, attempt to clean up.
     *   This may leave empty lists on the job and plugin, but those will
     *   be ultimately destroyed when the job is inactive or theplugin is
     *   unloaded.
     */
    if (!zlistx_add_end (ps->jobs, job)
        || !zlistx_add_end (job->subscribers, p)) {
        plugin_clear_subscription (p, job);
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

int job_event_id_set (struct job *job, int id)
{
    if (id < 0) {
        errno = EINVAL;
        return -1;
    }
    if (id >= EVENTS_BITMAP_SIZE) {
        errno = ENOSPC;
        return -1;
    }
    bitmap_set_bit (job->events, id);
    return 0;
}

int job_event_id_test (struct job *job, int id)
{
    if (id < 0 || id >= EVENTS_BITMAP_SIZE) {
        errno = EINVAL;
        return -1;
    }
    if (bitmap_test_bit (job->events, id))
        return 1;
    return 0;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

