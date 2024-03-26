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
#include "src/common/libutil/errprintf.h"
#include "ccan/str/str.h"

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
        json_decref (job->R_redacted);
        json_decref (job->eventlog);
        json_decref (job->annotations);
        grudgeset_destroy (job->dependencies);
        subscribers_destroy (job);
        free (job->events);
        aux_destroy (&job->aux);
        json_decref (job->event_queue);
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

static struct job *job_alloc (void)
{
    struct job *job;

    if (!(job = calloc (1, sizeof (*job))))
        return NULL;
    if (!(job->events = bitmap_alloc0 (EVENTS_BITMAP_SIZE)))
        goto error;
    job->refcount = 1;
    job->userid = FLUX_USERID_UNKNOWN;
    job->urgency = FLUX_JOB_URGENCY_DEFAULT;
    job->priority = -1;
    job->state = FLUX_JOB_STATE_NEW;
    if (!(job->event_queue = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    return job;
error:
    job_decref (job);
    return NULL;
}

struct job *job_create (void)
{
    struct job *job;

    if (!(job = job_alloc ()))
        return NULL;
    if (!(job->eventlog = json_array ())) {
        errno = ENOMEM;
        job_decref (job);
        return NULL;
    }
    return job;
}

int job_dependency_count (struct job *job)
{
    return grudgeset_size (job->dependencies);
}

int job_dependency_add (struct job *job, const char *description)
{
    assert (job->state == FLUX_JOB_STATE_NEW
            || job->state == FLUX_JOB_STATE_DEPEND);
    if (grudgeset_add (&job->dependencies, description) < 0
        && errno != EEXIST)
        return -1;
    return job_dependency_count (job);
}

int job_dependency_remove (struct job *job, const char *description)
{
    return grudgeset_remove (job->dependencies, description);
}

static int job_flag_set_internal (struct job *job,
                                  const char *flag,
                                  bool dry_run)
{
   if (streq (flag, "alloc-bypass")) {
        if (!dry_run)
            job->alloc_bypass = 1;
    }
    else if (streq (flag, "debug")) {
        if (!dry_run)
            job->flags |= FLUX_JOB_DEBUG;
    }
    else if (streq (flag, "immutable")) {
        if (!dry_run)
            job->immutable = 1;
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

void job_aux_destroy (struct job *job)
{
    aux_destroy (&job->aux);
}

static int jobspec_redacted_parse_queue (struct job *job)
{
    if (job->jobspec_redacted) {
        /* unit tests assume empty jobspec legal, so all fields
         * optional
         */
        if (json_unpack (job->jobspec_redacted,
                         "{s?{s?{s?s}}}",
                         "attributes",
                           "system",
                             "queue", &job->queue) < 0) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

struct job *job_create_from_eventlog (flux_jobid_t id,
                                      const char *eventlog,
                                      const char *jobspec,
                                      const char *R,
                                      flux_error_t *error)
{
    struct job *job;
    size_t index;
    json_t *event;
    int version = -1; // invalid

    if (!(job = job_alloc()))
        return NULL;
    job->id = id;

    if (!(job->jobspec_redacted = json_loads (jobspec, 0, NULL))) {
        errprintf (error, "failed to decode jobspec");
        goto inval;
    }
    jpath_del (job->jobspec_redacted, "attributes.system.environment");

    if (jobspec_redacted_parse_queue (job) < 0) {
        errprintf (error, "failed to decode jobspec queue");
        goto inval;
    }

    if (R) {
        if (!(job->R_redacted = json_loads (R, 0, NULL))) {
            errprintf (error, "failed to decode R");
            goto inval;
        }
        (void)json_object_del (job->R_redacted, "scheduling");
    }

    if (!(job->eventlog = eventlog_decode (eventlog))) {
        errprintf (error, "failed to decode eventlog");
        goto error;
    }

    json_array_foreach (job->eventlog, index, event) {
        const char *name = "unknown";
        json_t *context;

        if (index == 0) {
            if (eventlog_entry_parse (event, NULL, &name, &context) < 0) {
                errprintf (error, "eventlog parse error on line %zu", index);
                goto error;
            }
            if (!streq (name, "submit")) {
                errprintf (error, "first event is %s not submit", name);
                goto inval;
            }
            /* For now, support flux-core versions prior to 0.41.1 that don't
             * have a submit version attr, as is now required by RFC 21.
             * Allow version=-1 to pass through for work around below.
             */
            (void)json_unpack (context, "{s?i}", "version", &version);
            if (version != -1 && version != 1) {
                errprintf (error, "eventlog v%d is unsupported", version);
                goto inval;
            }
        }

        if (event_job_update (job, event) < 0) {
            errprintf (error, "could not apply %s", name);
            goto error;
        }

        /* Work around flux-framework/flux-core#4398.
         * "submit" used to transition NEW->DEPEND in unversioned eventlog,
         * but as of version 1, "validate" is required to transition.  Allow
         * old jobs to be ingested from the KVS by flux-core 0.41.1+.
         */
        if (index == 0 && version == -1)
            job->state = FLUX_JOB_STATE_DEPEND;

        job->eventlog_seq++;
    }

    if (job->state == FLUX_JOB_STATE_NEW) {
        errprintf (error,
                   "job state (%s) is invalid after replay",
                   flux_job_statetostr (job->state, "L"));
        goto inval;
    }

    return job;
inval:
    errno = EINVAL;
error:
    job_decref (job);
    return NULL;
}

struct job *job_create_from_json (json_t *o)
{
    struct job *job;

    if (!(job = job_create ()))
        return NULL;
    if (json_unpack (o,
                     "{s:I s:i s:i s:f s:i s:O}",
                     "id", &job->id,
                     "urgency", &job->urgency,
                     "userid", &job->userid,
                     "t_submit", &job->t_submit,
                     "flags", &job->flags,
                     "jobspec", &job->jobspec_redacted) < 0) {
        errno = EPROTO;
        job_decref (job);
        return NULL;
    }
    if (jobspec_redacted_parse_queue (job) < 0) {
        job_decref (job);
        return NULL;
    }
    return job;
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

/* Compare inactive jobs, ordering by the time they became inactive.
 * N.B. zlistx_comparator_fn signature
 */
int job_age_comparator (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j1->t_clean, j2->t_clean);
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

int job_event_enqueue (struct job *job, int flags, json_t *entry)
{
    json_t *wrap;

    if (job->eventlog_readonly) {
        errno = EROFS;
        return -1;
    }
    if (!(wrap = json_pack ("{s:i s:O}",
                            "flags", flags,
                            "entry", entry))
        || json_array_append_new (job->event_queue, wrap) < 0) {
        json_decref (wrap);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int job_event_peek (struct job *job, int *flagsp, json_t **entryp)
{
    json_t *wrap;
    json_t *entry;
    int flags;

    if (!(wrap = json_array_get (job->event_queue, 0))) {
        errno = ENOENT;
        return -1; // queue empty
    }
    if (json_unpack (wrap,
                     "{s:i s:o}",
                     "flags", &flags,
                     "entry", &entry) < 0) {
        errno = EPROTO;
        return -1;
    }
    if (entryp)
        *entryp = entry;
    if (flagsp)
        *flagsp = flags;
    return 0;
}

int job_event_dequeue (struct job *job, int *flagsp, json_t **entryp)
{
    json_t *entry;
    int flags;

    if (job_event_peek (job, &flags, &entry) < 0)
        return -1;
    if (entryp)
        *entryp = json_incref (entry);
    if (flagsp)
        *flagsp = flags;
    json_array_remove (job->event_queue, 0);
    return 0;
}

bool job_event_is_queued (struct job *job, const char *name)
{
    size_t index;
    json_t *wrap;
    json_t *entry;
    const char *entry_name;

    json_array_foreach (job->event_queue, index, wrap) {
        if (json_unpack (wrap, "{s:o}", "entry", &entry) == 0
            && eventlog_entry_parse (entry, NULL, &entry_name, NULL) == 0
            && streq (entry_name, name))
            return true;
    }
    return false;
}

const char *job_event_queue_print (struct job *job, char *buf, int size)
{
    size_t index;
    json_t *wrap;
    json_t *entry;
    const char *name;
    size_t used = 0;
    size_t n;

    buf[0] = '\0';
    json_array_foreach (job->event_queue, index, wrap) {
        if (json_unpack (wrap, "{s:o}", "entry", &entry) < 0
            || eventlog_entry_parse (entry, NULL, &name, NULL) < 0)
            name = "unknown";
        n = snprintf (buf + used,
                      size - used,
                      "%s%s",
                      index > 0 ? "/" : "",
                      name);
        if (n >= size - used)
            break;
        used += n;
    }
    return buf;
}

bool validate_jobspec_updates (json_t *updates)
{
    const char *key;
    json_t *entry;
    json_object_foreach (updates, key, entry) {
        if (!streq (key, "attributes")
            && !strstarts (key, "attributes.")
            && !streq (key, "resources")
            && !strstarts (key, "resources.")
            && !streq (key, "tasks")
            && !strstarts (key, "tasks."))
            return false;
    }
    return true;
}

static int jobspec_apply_updates (json_t *jobspec, json_t *updates)
{
    const char *path;
    json_t *val;

    if (!jobspec) {
        errno = EINVAL;
        return -1;
    }
    json_object_foreach (updates, path, val) {
        if (jpath_set (jobspec, path, val) < 0)
            return -1;
    }
    return 0;
}

int job_apply_jobspec_updates (struct job *job, json_t *updates)
{
    if (jobspec_apply_updates (job->jobspec_redacted, updates) < 0
        || jobspec_redacted_parse_queue (job) < 0)
        return -1;
    return 0;
}

json_t *job_jobspec_with_updates (struct job *job, json_t *updates)
{
    json_t *jobspec;

    if (!job->jobspec_redacted) {
        errno = EAGAIN;
        return NULL;
    }
    if (!(jobspec = json_deep_copy (job->jobspec_redacted))) {
        errno = ENOMEM;
        return NULL;
    }
    if (jobspec_apply_updates (jobspec, updates) < 0) {
        int saved_errno = errno;
        json_decref (jobspec);
        errno = saved_errno;
        return NULL;
    }
    return jobspec;
}

int job_apply_resource_updates (struct job *job, json_t *updates)
{
    json_t *val;

    if (!job->R_redacted) {
        errno = EAGAIN;
        return -1;
    }
    /* Currently only an expiration key is allowed in a resource-update
     * event. Return an error with errno=EINVAL if there is more than one
     * key in the updates object or the existing key is not 'expiration':
     */
    if (json_object_size (updates) != 1
        || !(val = json_object_get (updates, "expiration"))
        || !json_is_number (val)
        || json_number_value (val) < 0.) {
        errno = EINVAL;
        return -1;
    }
    /*  Update redacted copy of R in place.
     */
    return jpath_set (job->R_redacted, "execution.expiration", val);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

