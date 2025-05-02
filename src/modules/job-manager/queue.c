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
 *
 * The job manager currently has only one actual queue in alloc.c,
 * a vestigial design from before named queues.  Therefore, 'struct queue'
 * below is currently a container for queue state, not for jobs as one
 * might reasonably expect.
 *
 * Notes:
 * - By default, only a single anonymous queue is defined.  If any named queues
 *   are defined, the anonymous queue is removed.
 *
 * - A job requests to be in a particular queue by requiring the resource
 *   property associated with the nodes in the queue.  If it requires nothing,
 *   the anonymous queue is assumed.  the 'default' frobnicator plugin may be
 *   configured to add a default queue name when one is unspecified.
 *
 * - When a queue is enabled, jobs submitted for that queue are accepted.
 *   When it is disabled, the job submission program fails immediately.
 *
 * - When a queue is started, alloc requests for jobs in SCHED state are
 *   presented to the scheduler.  When it is stopped, those alloc requests
 *   are canceled.
 *
 * - After a queue is stopped, the job manager continues to send free
 *   requests to the scheduler for the queue as resources are released.
 *   Jobs/housekeeping are not canceled when a queue is stopped.
 *
 * - When a queue is enabled and stopped, job submissions to the queue are
 *   accepted, but the jobs will not run until the queue is started.
 *
 * See also:
 * RFC 33/Flux Job Queues
 * RFC 27/Resource Allocation Protocol Version 1
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

struct queue {
    char *name;
    bool is_enabled;    // jobs may be submitted to this queue
    char *disable_reason;   // reason if disabled
    bool is_started;    // current queue state
    bool is_started_sticky; // tracks is_started unless --nocheckpoint
    char *stop_reason; // reason if stopped (optionally set)
    json_t *requires;  // required properties array
};

struct queue_ctx {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    union {
        struct queue *anon;
        zhashx_t *named;
    };
    bool have_named_queues;
};

static void dequeue_jobs (struct queue_ctx *qctx, const char *name);

static void queue_destroy (struct queue *q)
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
static void queue_destructor (void **item)
{
    if (item) {
        queue_destroy (*item);
        *item = NULL;
    }
}

static struct queue *queue_create (const char *name, json_t *config)
{
    struct queue *q;

    if (!(q = calloc (1, sizeof (*q))))
        return NULL;
    if (name && !(q->name = strdup (name)))
        goto error;
    q->is_enabled = true;

    if (config && json_unpack (config, "{s?O}", "requires", &q->requires) < 0)
        goto error;

    /* The anonymous queue begins life started, while named queues do not.
     */
    if (name)
        q->is_started_sticky = q->is_started = false;
    else
        q->is_started_sticky = q->is_started = true;
    return q;
error:
    queue_destroy (q);
    return NULL;
}

static struct queue *queue_first (struct queue_ctx *qctx)
{
    if (qctx->have_named_queues)
        return zhashx_first (qctx->named);
    return qctx->anon;
}

static struct queue *queue_next (struct queue_ctx *qctx)
{
    if (qctx->have_named_queues)
        return zhashx_next (qctx->named);
    return NULL;
}

static int queue_enable (struct queue *q)
{
    q->is_enabled = true;
    free (q->disable_reason);
    q->disable_reason = NULL;
    return 0;
}

static int queue_disable (struct queue *q, const char *reason)
{
    char *cpy;
    if (!(cpy = strdup (reason)))
        return -1;
    free (q->disable_reason);
    q->disable_reason = cpy;
    q->is_enabled = false;
    return 0;
}

static int queue_start (struct queue *q, bool nocheckpoint)
{
    q->is_started = true;
    if (!nocheckpoint)
        q->is_started_sticky = q->is_started;
    free (q->stop_reason);
    q->stop_reason = NULL;
    return 0;
}

static int queue_stop (struct queue *q, const char *reason, bool nocheckpoint)
{
    char *cpy = NULL;
    if (reason) {
        if (!(cpy = strdup (reason)))
            return -1;
    }
    free (q->stop_reason);
    q->stop_reason = cpy;
    q->is_started = false;
    if (!nocheckpoint)
        q->is_started_sticky = q->is_started;
    return 0;
}

static int queue_enable_all (struct queue_ctx *qctx)
{
    struct queue *q = queue_first (qctx);
    while (q) {
        if (queue_enable (q) < 0)
            return -1;
        q = queue_next (qctx);
    }
    return 0;
}

static int queue_disable_all (struct queue_ctx *qctx, const char *reason)
{
    struct queue *q = queue_first (qctx);
    while (q) {
        if (queue_disable (q, reason) < 0)
            return -1;
        q = queue_next (qctx);
    }
    return 0;
}

static int queue_start_all (struct queue_ctx *qctx, bool nocheckpoint)
{
    struct queue *q = queue_first (qctx);
    while (q) {
        if (queue_start (q, nocheckpoint) < 0)
            return -1;
        q = queue_next (qctx);
    }
    return 0;
}

static int queue_stop_all (struct queue_ctx *qctx,
                           const char *reason,
                           bool nocheckpoint)
{
    struct queue *q = queue_first (qctx);
    while (q) {
        if (queue_stop (q, reason, nocheckpoint) < 0)
            return -1;
        q = queue_next (qctx);
    }
    dequeue_jobs (qctx, NULL);
    return 0;
}

static int queue_stop_one (struct queue_ctx *qctx,
                           struct queue *q,
                           const char *reason,
                           bool nocheckpoint)
{
    if (queue_stop (q, reason, nocheckpoint) < 0)
        return -1;
    dequeue_jobs (qctx, q->name);
    return 0;
}

struct queue *queue_lookup (struct queue_ctx *qctx,
                            const char *name,
                            flux_error_t *error)
{
    if (name) {
        struct queue *q;

        if (!qctx->have_named_queues
            || !(q = zhashx_lookup (qctx->named, name))) {
            errprintf (error, "'%s' is not a valid queue", name);
            return NULL;
        }
        return q;
    }
    else {
        if (qctx->have_named_queues) {
            errprintf (error, "a named queue is required");
            return NULL;
        }
        return qctx->anon;
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

static int queue_ctx_save_one (json_t *a, struct queue *q)
{
    json_t *entry;

    if (!(entry = json_pack ("{s:b s:b}",
                             "enable", q->is_enabled,
                             "start", q->is_started_sticky)))
        goto nomem;
    if (q->name) {
        if (set_string (entry, "name", q->name) < 0)
            goto error;
    }
    if (!entry)
        goto nomem;
    if (!q->is_enabled) {
        if (set_string (entry, "disable_reason", q->disable_reason) < 0)
            goto error;
    }
    if (!q->is_started_sticky && q->stop_reason) {
        if (set_string (entry, "stop_reason", q->stop_reason) < 0)
            goto error;
    }
    if (json_array_append_new (a, entry) < 0)
        goto nomem;
    return 0;
nomem:
    errno = ENOMEM;
error:
    ERRNO_SAFE_WRAP (json_decref, entry);
    return -1;
}

json_t *queue_ctx_save (struct queue_ctx *qctx)
{
    json_t *a;
    struct queue *q;

    if (!(a = json_array ())) {
        errno = ENOMEM;
        return NULL;
    }
    q = queue_first (qctx);
    while (q) {
        if (queue_ctx_save_one (a, q) < 0)
            goto error;
        q = queue_next (qctx);
    }
    return a;
error:
    ERRNO_SAFE_WRAP (json_decref, a);
    return NULL;
}

static int restore_state_v0 (struct queue_ctx *qctx, json_t *entry)
{
    const char *name = NULL;
    const char *reason = NULL;
    const char *disable_reason = NULL;
    int enable;
    struct queue *q = NULL;

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

    if ((q = queue_lookup (qctx, name, NULL))) {
        if (enable) {
            if (queue_enable (q) < 0)
                return -1;
        }
        else {
            if (queue_disable (q, disable_reason) < 0)
                return -1;
        }
    }
    return 0;
}

static int restore_state_v1 (struct queue_ctx *qctx, json_t *entry)
{
    const char *name = NULL;
    const char *disable_reason = NULL;
    const char *stop_reason = NULL;
    int enable;
    int start;
    struct queue *q = NULL;

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
    if (name && qctx->have_named_queues)
        q = zhashx_lookup (qctx->named, name);
    else if (!name && !qctx->have_named_queues)
        q = qctx->anon;
    if (q) {
        if (enable) {
            if (queue_enable (q) < 0)
                return -1;
        }
        else {
            if (queue_disable (q, disable_reason) < 0)
                return -1;
        }
        if (start) {
            if (queue_start (q, false) < 0)
                return -1;
        }
        else {
            if (queue_stop_one (qctx, q, stop_reason, false) < 0)
                return -1;
        }
    }
    return 0;
}

int queue_ctx_restore (struct queue_ctx *qctx, int version, json_t *o)
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
            if (restore_state_v0 (qctx, entry) < 0)
                return -1;
        }
        else { /* version == 1 */
            if (restore_state_v1 (qctx, entry) < 0)
                return -1;
        }
    }
    return 0;
}

int queue_submit_check (struct queue_ctx *qctx,
                        json_t *jobspec,
                        flux_error_t *error)
{
    struct queue *q;
    json_t *o;
    const char *name = NULL;

    if ((o = jpath_get (jobspec, "attributes.system.queue")))
        name = json_string_value (o);

    if (!(q = queue_lookup (qctx, name, error))) {
        errno = EINVAL;
        return -1;
    }
    if (!q->is_enabled) {
        errprintf (error, "job submission%s%s is disabled: %s",
                   name ? " to " : "",
                   name ? name : "",
                   q->disable_reason);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

bool queue_started (struct queue_ctx *qctx, struct job *job)
{
    if (qctx->have_named_queues) {
        struct queue *q;
        if (!job->queue)
            return false;
        if (!(q = zhashx_lookup (qctx->named, job->queue))) {
            flux_log (qctx->ctx->h, LOG_ERR,
                      "%s: job %s invalid queue: %s",
                      __FUNCTION__, idf58 (job->id), job->queue);
            return false;
        }
        return q->is_started;
    }

    return qctx->anon->is_started;
}

/* N.B. the basic queue configuration should have already been validated by
 * policy_validate() so we shouldn't need to produce detailed configuration
 * errors for users here.
 */
static int queue_configure (const flux_conf_t *conf,
                            flux_error_t *error,
                            void *arg)
{
    struct queue_ctx *qctx = arg;
    json_t *queues;

    if (flux_conf_unpack (conf, NULL, "{s:o}", "queues", &queues) == 0
        && json_object_size (queues) > 0) {
        const char *name;
        json_t *value;
        struct queue *q;
        zlistx_t *keys;

        /* destroy anon queue and create hash if necessary
         */
        if (!qctx->have_named_queues) {
            qctx->have_named_queues = true;
            queue_destroy (qctx->anon);
            if (!(qctx->named = zhashx_new ()))
                goto nomem;
            zhashx_set_destructor (qctx->named, queue_destructor);
        }
        /* remove any queues that disappeared from config
         */
        if (!(keys = zhashx_keys (qctx->named)))
            goto nomem;
        name = zlistx_first (keys);
        while (name) {
            if (!json_object_get (queues, name))
                zhashx_delete (qctx->named, name);
            name = zlistx_next (keys);
        }
        zlistx_destroy (&keys);
        /* add any new queues that appeared in config.  Note that
         * named queues default to being enabled/stopped.  On initial
         * module load, job-manager may change that state based on
         * prior checkpointed information.
         */
        json_object_foreach (queues, name, value) {
            if (!zhashx_lookup (qctx->named, name)) {
                if (!(q = queue_create (name, value)))
                    goto nomem;
                (void)zhashx_insert (qctx->named, name, q);
            }
        }
    }
    else {
        if (qctx->have_named_queues) {
            qctx->have_named_queues = false;
            zhashx_destroy (&qctx->named);
            if (!(qctx->anon = queue_create (NULL, NULL)))
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
    struct queue_ctx *qctx = arg;
    struct queue *q;
    json_t *a = NULL;;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(a = json_array ())) {
        errno = ENOMEM;
        goto error;
    }
    if (qctx->have_named_queues) {
        q = zhashx_first (qctx->named);
        while (q) {
            json_t *o;
            if (!(o = json_string (q->name))
                || json_array_append_new (a, o) < 0) {
                json_decref (o);
                errno = ENOMEM;
                goto error;
            }
            q = zhashx_next (qctx->named);
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
    struct queue_ctx *qctx = arg;
    flux_error_t error;
    const char *errmsg = NULL;
    const char *name = NULL;
    struct queue *q;
    json_t *o = NULL;
    bool start;
    const char *stop_reason = NULL;

    if (flux_request_unpack (msg, NULL, "{s?s}", "name", &name) < 0)
        goto error;
    if (!(q = queue_lookup (qctx, name, &error))) {
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    /* If the scheduler is not loaded the queue is considered stopped
     * with special reason "Scheduler is offline".
     */
    if (!alloc_sched_ready (qctx->ctx->alloc)) {
        start = false;
        stop_reason = "Scheduler is offline";
    }
    else {
        start = q->is_started;
        stop_reason = q->stop_reason;
    }
    if (!(o = json_pack ("{s:b s:b}",
                         "enable", q->is_enabled,
                         "start", start))) {
        errno = ENOMEM;
        goto error;
    }
    if (!q->is_enabled) {
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
    struct queue_ctx *qctx = arg;
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
        if (qctx->have_named_queues && !all) {
            errmsg = "Use --all to apply this command to all queues";
            errno = EINVAL;
            goto error;
        }
        if (enable) {
            if (queue_enable_all (qctx))
                goto error;
        }
        else {
            if (queue_disable_all (qctx, disable_reason))
                goto error;
        }
    }
    else {
        struct queue *q;
        if (!(q = queue_lookup (qctx, name, &error))) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
        if (enable) {
            if (queue_enable (q) < 0)
                goto error;
        }
        else {
            if (queue_disable (q, disable_reason) < 0)
                goto error;
        }
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-enable");
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "error responding to job-manager.queue-enable");
}

static int enqueue_jobs (struct queue_ctx *qctx, const char *name)
{
    struct job *job = zhashx_first (qctx->ctx->active_jobs);
    while (job) {
        if (!name || (job->queue && streq (job->queue, name))) {
            if (!job->alloc_queued
                && !job->alloc_pending
                && job->state == FLUX_JOB_STATE_SCHED) {
                if (alloc_enqueue_alloc_request (qctx->ctx->alloc, job) < 0)
                    return -1;
                if (alloc_queue_recalc_pending (qctx->ctx->alloc) < 0)
                    return -1;
            }
        }
        job = zhashx_next (qctx->ctx->active_jobs);
    }
    return 0;
}

static void dequeue_jobs (struct queue_ctx *qctx, const char *name)
{
    if (alloc_queue_count (qctx->ctx->alloc) > 0
        || alloc_pending_count (qctx->ctx->alloc) > 0) {
        struct job *job = zhashx_first (qctx->ctx->active_jobs);
        while (job) {
            if (!name || (job->queue && streq (job->queue, name))) {
                if (job->alloc_queued)
                    alloc_dequeue_alloc_request (qctx->ctx->alloc, job);
                else if (job->alloc_pending)
                    alloc_cancel_alloc_request (qctx->ctx->alloc, job, false);
            }
            job = zhashx_next (qctx->ctx->active_jobs);
        }
    }
}

static void queue_start_cb (flux_t *h,
                            flux_msg_handler_t *mh,
                            const flux_msg_t *msg,
                            void *arg)
{
    struct queue_ctx *qctx = arg;
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
        if (qctx->have_named_queues && !all) {
            errmsg = "Use --all to apply this command to all queues";
            errno = EINVAL;
            goto error;
        }
        if (start) {
            if (queue_start_all (qctx, nocheckpoint))
                goto error;
            if (enqueue_jobs (qctx, NULL) < 0)
                goto error;
        }
        else {
            if (queue_stop_all (qctx, stop_reason, nocheckpoint))
                goto error;
        }
    }
    else {
        struct queue *q;
        if (!(q = queue_lookup (qctx, name, &error))) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
        if (start) {
            if (queue_start (q, nocheckpoint) < 0)
                goto error;
            if (enqueue_jobs (qctx, name) < 0)
                goto error;
        }
        else {
            if (queue_stop_one (qctx, q, stop_reason, nocheckpoint) < 0)
                goto error;
        }
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

void queue_ctx_destroy (struct queue_ctx *qctx)
{
    if (qctx) {
        int saved_errno = errno;
        conf_unregister_callback (qctx->ctx->conf, queue_configure);
        flux_msg_handler_delvec (qctx->handlers);
        if (qctx->have_named_queues)
            zhashx_destroy (&qctx->named);
        else
            queue_destroy (qctx->anon);
        free (qctx);
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

static int constraints_match_check (struct queue_ctx *qctx,
                                    const char *name,
                                    json_t *constraints,
                                    flux_error_t *errp)
{
    int rc = -1;
    json_t *expected = NULL;
    struct queue *q;

    /*  Return an error if the job's current queue doesn't exist since we
     *  can't validate current constraints (This should not happen in normal
     *  situations).
     */
    if (!(q = queue_lookup (qctx, name, errp)))
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
    struct queue_ctx *qctx = arg;
    flux_job_state_t state;
    const char *name;
    const char *current_queue = NULL;
    json_t *constraints = NULL;
    flux_error_t error;
    struct queue *newq;

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
    if (!(newq = queue_lookup (qctx, name, &error))) {
        flux_jobtap_error (p, args, "%s", error.text);
        return -1;
    }
    if (!newq->is_enabled) {
        flux_jobtap_error (p,
                           args,
                           "queue %s is currently disabled",
                           name);
        return -1;
    }
    /*  Constraints must match current queue exactly since they will be
     *  overwritten with new queue constraints after queue is updated:
     */
    if (constraints_match_check (qctx, current_queue, constraints, &error)) {
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

struct queue_ctx *queue_ctx_create (struct job_manager *ctx)
{
    struct queue_ctx *qctx;
    flux_error_t error;

    if (!(qctx = calloc (1, sizeof (*qctx))))
        return NULL;
    qctx->ctx = ctx;
    if (!(qctx->anon = queue_create (NULL, NULL)))
        goto error;
    if (flux_msg_handler_addvec (ctx->h,
                                 htab,
                                 qctx,
                                 &qctx->handlers) < 0)
        goto error;
    if (conf_register_callback (ctx->conf,
                                &error,
                                queue_configure,
                                qctx) < 0) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "error parsing queue config: %s",
                  error.text);
        goto error;
    }
    if (jobtap_register_builtin (ctx->jobtap,
                                 ".update-queue",
                                 update_queue_plugin_init,
                                 qctx) < 0
        || !jobtap_load (ctx->jobtap, ".update-queue", NULL, NULL)) {
        flux_log (ctx->h,
                  LOG_ERR,
                  "Failed to register and load update-queue plugin");
        goto error;
    }
    return qctx;
error:
    queue_ctx_destroy (qctx);
    return NULL;
}

// vi:ts=4 sw=4 expandtab
