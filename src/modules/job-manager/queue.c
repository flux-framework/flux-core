/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queue.c - job queues
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <flux/core.h>

#include "src/common/libutil/errprintf.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "alloc.h"
#include "job-manager.h"
#include "jobtap-internal.h"
#include "jobtap.h"
#include "conf.h"
#include "restart.h"
#include "queue.h"

/* What it means to be administratively stopped:
 *
 * While allocation is stopped, the scheduler can remain loaded and
 * handle requests, but the job manager won't send any more allocation
 * requests.  Pending alloc requests are canceled.  The job manager
 * continues to send free requests to the scheduler as jobs relinquish
 * resources.
 */
struct jobq {
    char *name;
    bool enable;    // jobs may be submitted to this queue
    char *disable_reason;   // reason if disabled
    bool start;
    bool checkpoint_start;  // may be different that actual start due
                            // to nocheckpoint flag
    char *stop_reason; // reason if stopped (optionally set)
    json_t *requires;  // required properties array
};

struct queue {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    union {
        struct jobq *anon;
        zhashx_t *named;
    };
    bool have_named_queues;
};

static void jobq_destroy (struct jobq *q)
{
    if (q) {
        int saved_errno = errno;
        json_decref (q->requires);
        free (q->name);
        free (q->disable_reason);
        free (q);
        errno = saved_errno;
    }
}

// zhashx_destructor_fn signature
static void jobq_destructor (void **item)
{
    if (item) {
        jobq_destroy (*item);
        *item = NULL;
    }
}

static struct jobq *jobq_create (const char *name, json_t *config)
{
    struct jobq *q;

    if (!(q = calloc (1, sizeof (*q))))
        return NULL;
    if (name && !(q->name = strdup (name)))
        goto error;
    q->enable = true;

    if (config && json_unpack (config, "{s?O}", "requires", &q->requires) < 0)
        goto error;

    if (name) {
        q->start = false;
        q->checkpoint_start = false;
    }
    else {
        q->start = true;
        q->checkpoint_start = true;
    }
    return q;
error:
    jobq_destroy (q);
    return NULL;
}

static int jobq_enable (struct jobq *q,
                        bool enable,
                        const char *disable_reason)
{
    if (enable) {
        q->enable = true;
        free (q->disable_reason);
        q->disable_reason = NULL;
    }
    else {
        char *cpy;
        if (!(cpy = strdup (disable_reason)))
            return -1;
        free (q->disable_reason);
        q->disable_reason = cpy;
        q->enable = false;
    }
    return 0;
}

static int jobq_start (struct jobq *q,
                       bool start,
                       const char *stop_reason,
                       bool nocheckpoint)
{
    if (start) {
        q->start = true;
        if (!nocheckpoint)
            q->checkpoint_start = true;
        free (q->stop_reason);
        q->stop_reason = NULL;
    }
    else {
        char *cpy = NULL;
        if (stop_reason) {
            if (!(cpy = strdup (stop_reason)))
                return -1;
        }
        free (q->stop_reason);
        q->stop_reason = cpy;
        q->start = false;
        if (!nocheckpoint)
            q->checkpoint_start = false;
    }
    return 0;
}

static int jobq_enable_all (struct queue *queue,
                            bool enable,
                            const char *disable_reason)
{
    if (queue->have_named_queues) {
        struct jobq *q = zhashx_first (queue->named);
        while (q) {
            if (jobq_enable (q, enable, disable_reason) < 0)
                return -1;
            q = zhashx_next (queue->named);
        }
    }
    else {
        if (jobq_enable (queue->anon, enable, disable_reason) < 0)
            return -1;
    }
    return 0;
}

static int jobq_start_all (struct queue *queue,
                           bool start,
                           const char *stop_reason,
                           bool nocheckpoint)
{
    if (queue->have_named_queues) {
        struct jobq *q = zhashx_first (queue->named);
        while (q) {
            if (jobq_start (q, start, stop_reason, nocheckpoint) < 0)
                return -1;
            q = zhashx_next (queue->named);
        }
    }
    else {
        if (jobq_start (queue->anon, start, stop_reason, nocheckpoint) < 0)
            return -1;
    }
    return 0;
}

struct jobq *queue_lookup (struct queue *queue,
                           const char *name,
                           flux_error_t *error)
{
    if (name) {
        struct jobq *q;

        if (!queue->have_named_queues
            || !(q = zhashx_lookup (queue->named, name))) {
            errprintf (error, "'%s' is not a valid queue", name);
            return NULL;
        }
        return q;
    }
    else {
        if (queue->have_named_queues) {
            errprintf (error, "a named queue is required");
            return NULL;
        }
        return queue->anon;
    }
}

static int set_string (json_t *o, const char *key, const char *val)
{
    json_t *s = json_string (val);
    if (!s || json_object_set_new (o, key, s) < 0) {
        json_decref (s);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int queue_save_jobq_append (json_t *a, struct jobq *q)
{
    json_t *entry;
    int save_errno;
    if (!q->name)
        entry = json_pack ("{s:b s:b}",
                           "enable", q->enable,
                           "start", q->checkpoint_start);
    else
        entry = json_pack ("{s:s s:b s:b}",
                           "name", q->name,
                           "enable", q->enable,
                           "start", q->checkpoint_start);
    if (!entry)
        goto nomem;
    if (!q->enable) {
        if (set_string (entry, "disable_reason", q->disable_reason) < 0)
            goto error;
    }
    if (!q->checkpoint_start && q->stop_reason) {
        if (set_string (entry, "stop_reason", q->stop_reason) < 0)
            goto error;
    }
    if (json_array_append_new (a, entry) < 0)
        goto nomem;
    return 0;
nomem:
    errno = ENOMEM;
error:
    save_errno = errno;
    json_decref (entry);
    errno = save_errno;
    return -1;
}

json_t *queue_save_state (struct queue *queue)
{
    json_t *a;
    struct jobq *q;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }
    if (queue->have_named_queues) {
        q = zhashx_first (queue->named);
        while (q) {
            if (queue_save_jobq_append (a, q) < 0)
                goto error;
            q = zhashx_next (queue->named);
        }
    }
    else {
        if (queue_save_jobq_append (a, queue->anon) < 0)
            goto error;
    }
    return a;
error:
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

static int restore_state_v0 (struct queue *queue, json_t *entry)
{
    const char *name = NULL;
    const char *reason = NULL;
    const char *disable_reason = NULL;
    int enable;
    struct jobq *q = NULL;

    if (json_unpack (entry,
                     "{s?s s:b s?s s?s}",
                     "name", &name,
                     "enable", &enable,
                     "reason", &reason,
                     "disable_reason", &disable_reason) < 0) {
        errno = EINVAL;
        return -1;
    }

    /* "reason" is backwards compatible field name for "disable_reason" */
    if (!disable_reason && reason)
        disable_reason = reason;
    if (name && queue->have_named_queues)
        q = zhashx_lookup (queue->named, name);
    else if (!name && !queue->have_named_queues)
        q = queue->anon;
    if (q) {
        if (jobq_enable (q, enable, disable_reason) < 0)
            return -1;
    }
    return 0;
}

static int restore_state_v1 (struct queue *queue, json_t *entry)
{
    const char *name = NULL;
    const char *disable_reason = NULL;
    const char *stop_reason = NULL;
    int enable;
    int start;
    struct jobq *q = NULL;

    if (json_unpack (entry,
                     "{s?s s:b s?s s:b s?s}",
                     "name", &name,
                     "enable", &enable,
                     "disable_reason", &disable_reason,
                     "start", &start,
                     "stop_reason", &stop_reason) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (name && queue->have_named_queues)
        q = zhashx_lookup (queue->named, name);
    else if (!name && !queue->have_named_queues)
        q = queue->anon;
    if (q) {
        if (jobq_enable (q, enable, disable_reason) < 0)
            return -1;
        if (jobq_start (q, start, stop_reason, false) < 0)
            return -1;
    }
    return 0;
}

int queue_restore_state (struct queue *queue, int version, json_t *o)
{
    size_t index;
    json_t *entry;

    if ((version != 0 && version != 1)
        || !o
        || !json_is_array (o)) {
        errno = EINVAL;
        return -1;
    }
    json_array_foreach (o, index, entry) {
        if (version == 0) {
            if (restore_state_v0 (queue, entry) < 0)
                return -1;
        }
        else { /* version == 1 */
            if (restore_state_v1 (queue, entry) < 0)
                return -1;
        }
    }
    return 0;
}

int queue_submit_check (struct queue *queue,
                        json_t *jobspec,
                        flux_error_t *error)
{
    struct jobq *q;
    json_t *o;
    const char *name = NULL;

    if ((o = jpath_get (jobspec, "attributes.system.queue")))
        name = json_string_value (o);

    if (!(q = queue_lookup (queue, name, error))) {
        errno = EINVAL;
        return -1;
    }
    if (!q->enable) {
        errprintf (error, "job submission%s%s is disabled: %s",
                   name ? " to " : "",
                   name ? name : "",
                   q->disable_reason);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

bool queue_started (struct queue *queue, struct job *job)
{
    if (queue->have_named_queues) {
        struct jobq *q;
        if (!job->queue)
            return false;
        if (!(q = zhashx_lookup (queue->named, job->queue))) {
            flux_log (queue->ctx->h, LOG_ERR,
                      "%s: job %s invalid queue: %s",
                      __FUNCTION__, idf58 (job->id), job->queue);
            return false;
        }
        return q->start;
    }

    return queue->anon->start;
}

/* N.B. the basic queue configuration should have already been validated by
 * policy_validate() so we shouldn't need to produce detailed configuration
 * errors for users here.
 */
static int queue_configure (const flux_conf_t *conf,
                            flux_error_t *error,
                            void *arg)
{
    struct queue *queue = arg;
    json_t *queues;

    if (flux_conf_unpack (conf, NULL, "{s:o}", "queues", &queues) == 0
        && json_object_size (queues) > 0) {
        const char *name;
        json_t *value;
        struct jobq *q;
        zlistx_t *keys;

        /* destroy anon queue and create hash if necessary
         */
        if (!queue->have_named_queues) {
            queue->have_named_queues = true;
            jobq_destroy (queue->anon);
            if (!(queue->named = zhashx_new ()))
                goto nomem;
            zhashx_set_destructor (queue->named, jobq_destructor);
        }
        /* remove any queues that disappeared from config
         */
        if (!(keys = zhashx_keys (queue->named)))
            goto nomem;
        name = zlistx_first (keys);
        while (name) {
            if (!json_object_get (queues, name))
                zhashx_delete (queue->named, name);
            name = zlistx_next (keys);
        }
        zlistx_destroy (&keys);
        /* add any new queues that appeared in config.  Note that
         * named queues default to being enabled/stopped.  On initial
         * module load, job-manager may change that state based on
         * prior checkpointed information.
         */
        json_object_foreach (queues, name, value) {
            if (!zhashx_lookup (queue->named, name)) {
                if (!(q = jobq_create (name, value)))
                    goto nomem;
                (void)zhashx_insert (queue->named, name, q);
            }
        }
    }
    else {
        if (queue->have_named_queues) {
            queue->have_named_queues = false;
            zhashx_destroy (&queue->named);
            if (!(queue->anon = jobq_create (NULL, NULL)))
                goto nomem;
        }
    }
    return 1;
nomem:
    errprintf (error, "out of memory while processing queue configuration");
    errno = ENOMEM;
    return -1;
}

static void queue_list_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct queue *queue = arg;
    struct jobq *q;
    json_t *a = NULL;;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    if (queue->have_named_queues) {
        q = zhashx_first (queue->named);
        while (q) {
            json_t *o;
            if (!(o = json_string (q->name))
                || json_array_append_new (a, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
            q = zhashx_next (queue->named);
        }
    }
    if (flux_respond_pack (h, msg, "{s:O}", "queues", a) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (a);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (a);
}

static void queue_status_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct queue *queue = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    struct jobq *q;
    json_t *o = NULL;
    bool start;
    const char *stop_reason = NULL;

    if (flux_request_unpack (msg, NULL, "{s?s}", "name", &name) < 0)
        goto error;
    if (!(q = queue_lookup (queue, name, &error))) {
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    /* If the scheduler is not loaded the queue is considered stopped
     * with special reason "Scheduler is offline".
     */
    if (!alloc_sched_ready (queue->ctx->alloc)) {
        start = false;
        stop_reason = "Scheduler is offline";
    }
    else {
        start = q->start;
        stop_reason = q->stop_reason;
    }
    if (!(o = json_pack ("{s:b s:b}",
                         "enable", q->enable,
                         "start", start))) {
        errno = ENOMEM;
        goto error;
    }
    if (!q->enable) {
        if (set_string (o, "disable_reason", q->disable_reason) < 0)
            goto error;
    }
    if (!start && stop_reason) {
        if (set_string (o, "stop_reason", stop_reason) < 0)
            goto error;
    }
    if (flux_respond_pack (h, msg, "O", o) < 0)
        flux_log_error (h, "error responding to job-manager.queue-status");
    json_decref (o);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-status");
    json_decref (o);
}

static void queue_enable_cb (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct queue *queue = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    int enable;
    const char *disable_reason = NULL;
    int all;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:b s?s s:b}",
                             "name", &name,
                             "enable", &enable,
                             "reason", &disable_reason,
                             "all", &all) < 0)
        goto error;
    if (!enable && !disable_reason) {
        errmsg = "reason is required for disable";
        errno = EINVAL;
        goto error;
    }
    if (!name) {
        if (queue->have_named_queues && !all) {
            errmsg = "Use --all to apply this command to all queues";
            errno = EINVAL;
            goto error;
        }
        if (jobq_enable_all (queue, enable, disable_reason))
            goto error;
    }
    else {
        struct jobq *q;
        if (!(q = queue_lookup (queue, name, &error))) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
        if (jobq_enable (q, enable, disable_reason) < 0)
            goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-enable");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-enable");
}

static int queue_start (struct queue *queue, const char *name)
{
    struct job *job = zhashx_first (queue->ctx->active_jobs);
    while (job) {
        if (!name || (job->queue && streq (job->queue, name))) {
            if (!job->alloc_queued
                && !job->alloc_pending
                && job->state == FLUX_JOB_STATE_SCHED) {
                if (alloc_enqueue_alloc_request (queue->ctx->alloc, job) < 0)
                    return -1;
                if (alloc_queue_recalc_pending (queue->ctx->alloc) < 0)
                    return -1;
            }
        }
        job = zhashx_next (queue->ctx->active_jobs);
    }
    return 0;
}

static void queue_stop (struct queue *queue, const char *name)
{
    if (alloc_queue_count (queue->ctx->alloc) > 0
        || alloc_pending_count (queue->ctx->alloc) > 0) {
        struct job *job = zhashx_first (queue->ctx->active_jobs);
        while (job) {
            if (!name || (job->queue && streq (job->queue, name))) {
                if (job->alloc_queued)
                    alloc_dequeue_alloc_request (queue->ctx->alloc, job);
                else if (job->alloc_pending)
                    alloc_cancel_alloc_request (queue->ctx->alloc, job, false);
            }
            job = zhashx_next (queue->ctx->active_jobs);
        }
    }
}

static void queue_start_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct queue *queue = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    int start;
    const char *stop_reason = NULL;
    int all;
    int nocheckpoint = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s?s s:b s?s s:b s?b}",
                             "name", &name,
                             "start", &start,
                             "reason", &stop_reason,
                             "all", &all,
                             "nocheckpoint", &nocheckpoint) < 0)
        goto error;
    if (!name) {
        if (queue->have_named_queues && !all) {
            errmsg = "Use --all to apply this command to all queues";
            errno = EINVAL;
            goto error;
        }
        if (jobq_start_all (queue, start, stop_reason, nocheckpoint))
            goto error;
        if (start) {
            if (queue_start (queue, NULL) < 0)
                goto error;
        }
        else
            queue_stop (queue, NULL);
    }
    else {
        struct jobq *q;
        if (!(q = queue_lookup (queue, name, &error))) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
        if (jobq_start (q, start, stop_reason, nocheckpoint) < 0)
            goto error;
        if (start) {
            if (queue_start (queue, name) < 0)
                goto error;
        }
        else
            queue_stop (queue, name);
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-start");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-start");
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-list",
        queue_list_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-status",
        queue_status_cb,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-enable",
        queue_enable_cb,
        0,
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.queue-start",
        queue_start_cb,
        0,
    },
    FLUX_MSGHANDLER_TABLE_END,
};

void queue_destroy (struct queue *queue)
{
    if (queue) {
        int saved_errno = errno;
        conf_unregister_callback (queue->ctx->conf, queue_configure);
        flux_msg_handler_delvec (queue->handlers);
        if (queue->have_named_queues)
            zhashx_destroy (&queue->named);
        else
            jobq_destroy (queue->anon);
        free (queue);
        errno = saved_errno;
    }
}

/*  Test equality of two constraint objects.
 *  For now, two constraints are equivalent if:
 *
 *  - both are either NULL or empty objects (i.e. size == 0)
 *    (Note: json_object_size (NULL) == 0)
 *
 *  - json_equal(a, b) returns true
 */
static bool constraints_equal (json_t *c1, json_t *c2)
{
    if ((json_object_size (c1) == 0 && json_object_size (c2) == 0)
        || json_equal (c1, c2))
        return true;
    return false;
}

static int constraints_match_check (struct queue *queue,
                                    const char *name,
                                    json_t *constraints,
                                    flux_error_t *errp)
{
    int rc = -1;
    json_t *expected = NULL;
    struct jobq *q;

    /*  Return an error if the job's current queue doesn't exist since we
     *  can't validate current constraints (This should not happen in normal
     *  situations).
     */
    if (!(q = queue_lookup (queue, name, errp)))
        return -1;

    /*  If current queue has constraints, then create a constraint object
     *  for equivalence test below:
     */
    if (q->requires
        && !(expected = json_pack ("{s:O}", "properties", q->requires))) {
        errprintf (errp, "failed to get constraints for current queue");
        goto out;
    }

    /*  Constraints of current job and queue must match exactly or queue
     *  update will be rejected. This is because the entire constraints
     *  object will be overwritten on queue update, and we do not want to
     *  replace any extra constraints provided on the submission commandline
     *  (and these likely wouldn't make sense in the new queue anyway)
     */
    if (!constraints_equal (constraints, expected)) {
        errprintf (errp,
                   "job appears to have non-queue constraints, "
                   "unable to update queue to %s",
                   name);
        goto out;
    }
    rc = 0;
out:
    json_decref (expected);
    return rc;
}

static int queue_update_cb (flux_plugin_t *p,
                            const char *topic,
                            flux_plugin_arg_t *args,
                            void *arg)
{
    int rc;
    struct queue *queue = arg;
    flux_job_state_t state;
    const char *name;
    const char *current_queue = NULL;
    json_t *constraints = NULL;
    flux_error_t error;
    struct jobq *newq;

    if (flux_plugin_arg_unpack (args,
                                FLUX_PLUGIN_ARG_IN,
                                "{s:s s:i s:{s:{s:{s?s s?o}}}}",
                                "value", &name,
                                "state", &state,
                                "jobspec",
                                 "attributes",
                                  "system",
                                   "queue", &current_queue,
                                   "constraints", &constraints) < 0) {
        flux_jobtap_error (p, args, "plugin args unpack failed");
        return -1;
    }
    if (state == FLUX_JOB_STATE_RUN
        || state == FLUX_JOB_STATE_CLEANUP) {
        flux_jobtap_error (p,
                           args,
                           "update of queue for running job not supported");
        return -1;
    }
    if (current_queue && streq (current_queue, name)) {
        flux_jobtap_error (p,
                           args,
                           "job queue is already set to %s",
                           name);
        return -1;
    }
    if (!(newq = queue_lookup (queue, name, &error))) {
        flux_jobtap_error (p, args, "%s", error.text);
        return -1;
    }
    if (!newq->enable) {
        flux_jobtap_error (p,
                           args,
                           "queue %s is currently disabled",
                           name);
        return -1;
    }
    /*  Constraints must match current queue exactly since they will be
     *  overwritten with new queue constraints after queue is updated:
     */
    if (constraints_match_check (queue, current_queue, constraints, &error)) {
        flux_jobtap_error (p, args, "%s", error.text);
        return -1;
    }
    /*  Request the update service do a feasibility check for this update
     *  and append an additional update of the job constraints.
     *
     *  This is done via two different calls below dependent on whether the
     *  new queue has any constraints.
     */
    if (newq->requires) {
        /*  Replace current constraints with those of the new queue
         */
        rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_OUT,
                                   "{s:i s:{s:{s:O}}}",
                                   "feasibility", 1,
                                   "updates",
                                    "attributes.system.constraints",
                                     "properties", newq->requires);
    }
    else {
        /*  New queue has no requirements. Set constraints to empty object.
         */
        rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_OUT,
                                   "{s:i s:{s:{}}}",
                                   "feasibility", 1,
                                   "updates",
                                    "attributes.system.constraints");
    }
    /*  If either of the above packs failed then return an error:
     */
    if (rc < 0) {
        flux_jobtap_error (p,
                           args,
                           "unable to create jobtap out arguments");
        return -1;
    }
    return 0;
}

static int update_queue_plugin_init (flux_plugin_t *p, void *arg)
{
    return flux_plugin_add_handler (p,
                                    "job.update.attributes.system.queue",
                                    queue_update_cb,
                                    arg);
}

struct queue *queue_create (struct job_manager *ctx)
{
    struct queue *queue;
    flux_error_t error;

    if (!(queue = calloc (1, sizeof (*queue))))
        return NULL;
    queue->ctx = ctx;
    if (!(queue->anon = jobq_create (NULL, NULL)))
        goto error;
    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 queue,
                                 &queue->handlers) < 0)
        goto error;
    if (conf_register_callback (ctx->conf,
                                &error,
                                queue_configure,
                                queue) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "error parsing queue config: %s",
                  error.text);
        goto error;
    }
    if (jobtap_register_builtin (ctx->jobtap,
                                 ".update-queue",
                                 update_queue_plugin_init,
                                 queue) < 0
        || !jobtap_load (ctx->jobtap, ".update-queue", NULL, NULL)) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "Failed to register and load update-queue plugin");
        goto error;
    }
    return queue;
error:
    queue_destroy (queue);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
