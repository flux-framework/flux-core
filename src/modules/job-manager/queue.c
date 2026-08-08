/************************************************************\
 * Copyright 2022 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* queue.c - job queue service layer
 *
 * This is the service layer for job queues.  All queue-table state is
 * delegated to struct queues (queues.[ch]).  This file owns:
 *
 * - struct queue_ctx: job_manager back-pointer, msg handlers, queues
 *   object, and stop_on_restart flag.
 * - Four RPC callbacks: queue-list, queue-status, queue-enable, queue-start.
 * - queue_configure: flux_conf_t glue.
 * - enqueue_jobs/dequeue_jobs: driven from the change-notification callback.
 * - queue_submit_check: submission gate.
 * - queue_started: alloc helper.
 * - queue_ctx_save/restore: checkpoint delegation.
 * - .update-queue jobtap plugin.
 *
 * Notes:
 * - By default, only a single anonymous queue is defined.  If any named
 *   queues are defined, the anonymous queue is removed.
 *
 * - A job requests to be in a particular queue by requiring the resource
 *   property associated with the nodes in the queue.  If it requires nothing,
 *   the anonymous queue is assumed.  The 'default' frobnicator plugin may be
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
#include "src/common/libczmqcontainers/czmq_containers.h"
#include "ccan/str/str.h"

#include "alloc.h"
#include "job-manager.h"
#include "jobtap-internal.h"
#include "jobtap.h"
#include "conf.h"
#include "restart.h"
#include "queues.h"
#include "queue.h"

struct queue_ctx {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    struct queues *queues;
    bool stop_on_restart;   // stop started queues on restart
};

static int enqueue_jobs (struct queue_ctx *qctx, const char *name);
static void dequeue_jobs (struct queue_ctx *qctx, const char *name);

/* Queue change-notification callback: drives enqueue/dequeue side effects.
 *
 * Events and their actions:
 *  "start"  -> enqueue_jobs (name)
 *  "stop"   -> dequeue_jobs (name)
 *  "remove" -> no dequeue (leave active jobs in removed queue)
 *  all others -> no action
 *
 * Notifications are never fired while queues_restore() replays
 * checkpointed state, so these side effects apply only to live
 * administrative changes. queue_ctx_restore() cancels alloc requests
 * for queues restored in a stopped state.
 */
static void on_queue_change (struct queues *queues,
                             struct queue *q,
                             const char *event,
                             void *arg)
{
    struct queue_ctx *qctx = arg;

    if (streq (event, "start")) {
        if (enqueue_jobs (qctx, queue_name (q)) < 0)
            flux_log_error (qctx->ctx->h,
                            "error enqueueing jobs for started queue");
    }
    else if (streq (event, "stop"))
        dequeue_jobs (qctx, queue_name (q));
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
    json_t *config = NULL;

    /* N.B. both keys are optional, so unpack fails only on an internal
     * error such as out of memory.
     */
    if (flux_conf_unpack (conf,
                          error,
                          "{s?{s?b} s?o}",
                          "job-manager",
                            "stop-queues-on-restart",
                              &qctx->stop_on_restart,
                          "queues", &config) < 0)
        return -1;
    if (queues_configure (qctx->queues, config, error) < 0)
        return -1;
    return 1;
}

static void queue_list_cb (flux_t *h,
                           flux_msg_handler_t *mh,
                           const flux_msg_t *msg,
                           void *arg)
{
    struct queue_ctx *qctx = arg;
    json_t *resp = NULL;

    if (flux_request_decode (msg, NULL, NULL) < 0)
        goto error;
    if (!(resp = queues_list_response (qctx->queues)))
        goto error;
    if (flux_respond_pack (h, msg, "O", resp) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (resp);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to job-manager.queue-list");
    json_decref (resp);
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

    if (flux_request_unpack (msg, NULL, "{s?s}", "name", &name) < 0)
        goto error;
    if (!(q = queues_lookup (qctx->queues, name, &error))) {
        errmsg = error.text;
        errno = EINVAL;
        goto error;
    }
    if (!(o = queue_status_encode (q, alloc_sched_ready (qctx->ctx->alloc))))
        goto error;
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
    if (!name && queues_have_named (qctx->queues) && !all) {
        errmsg = "Use --all to apply this command to all queues";
        errno = EINVAL;
        goto error;
    }
    if (enable) {
        if (queues_enable_queue (qctx->queues, name, &error) < 0) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
    }
    else {
        if (queues_disable_queue (qctx->queues,
                                  name,
                                  disable_reason,
                                  &error) < 0) {
            errmsg = error.text;
            errno = EINVAL;
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

/* This function returns true if an operation on the queue named
 * 'name' (e.g. start or stop) applies to a job submitted to queue
 * 'job_queue'. It is used by enqueue_jobs()/dequeue_jobs() to select
 * jobs.
 *
 * A NULL 'name' applies to every job. Otherwise the operation
 * applies to the job when:
 *  - 'name' is the job's own queue, or
 *  - 'name' is a non-virtual queue and the job's queue is one of its
 *    virtual queues (RFC 33): a virtual queue's jobs are scheduled
 *    as part of the parent queue, so an operation on the parent must
 *    reach them. The converse is not true: an operation on a virtual
 *    queue does not apply to its parent's or sibling queues' jobs.
 *
 * If either name no longer resolves to a configured queue (e.g.
 * removed by a reload - such jobs are intentionally left in place,
 * see the on_queue_change() N.B. above), fall back to an exact name
 * comparison, matching pre-vqueue behavior.
 */
static bool queue_name_covers (struct queue_ctx *qctx,
                               const char *name,
                               const char *job_queue)
{
    struct queue *target;
    struct queue *jq;

    if (!name)
        return true;
    if (!job_queue)
        return false;
    if (!(target = queues_lookup (qctx->queues, name, NULL)))
        return streq (job_queue, name);
    if (!(jq = queues_lookup (qctx->queues, job_queue, NULL)))
        return streq (job_queue, name);
    if (queue_is_virtual (target))
        return jq == target;
    return queue_root (jq) == target;
}

static int enqueue_jobs (struct queue_ctx *qctx, const char *name)
{
    struct job *job = zhashx_first (qctx->ctx->active_jobs);
    while (job) {
        if (queue_name_covers (qctx, name, job->queue)) {
            if (!job->alloc_queued
                && !job->alloc_pending
                && job->state == FLUX_JOB_STATE_SCHED
                && queue_started (qctx, job)) {
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
            if (queue_name_covers (qctx, name, job->queue)) {
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
    if (!name && queues_have_named (qctx->queues) && !all) {
        errmsg = "Use --all to apply this command to all queues";
        errno = EINVAL;
        goto error;
    }
    if (start) {
        if (queues_start_queue (qctx->queues,
                                name,
                                nocheckpoint,
                                &error) < 0) {
            errmsg = error.text;
            errno = EINVAL;
            goto error;
        }
    }
    else {
        if (queues_stop_queue (qctx->queues,
                               name,
                               stop_reason,
                               nocheckpoint,
                               &error) < 0) {
            errmsg = error.text;
            errno = EINVAL;
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
        queues_destroy (qctx->queues);
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
    if (!(q = queues_lookup (qctx->queues, name, errp)))
        return -1;

    /*  If current queue has constraints, then create a constraint object
     *  for equivalence test below. queue_requires() returns the effective
     *  requires: for a virtual queue (RFC 33) its own requires is always
     *  NULL (enforced by conf_policy.c), but its jobs carry the parent's
     *  property constraint (injected by the constraints frobnicator
     *  plugin), so the parent's requires is what must match here.
     */
    if (queue_requires (q)
        && !(expected = json_pack ("{s:O}",
                                   "properties",
                                   queue_requires (q)))) {
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
    if (!(newq = queues_lookup (qctx->queues, name, &error))) {
        flux_jobtap_error (p, args, "%s", error.text);
        return -1;
    }
    if (!queue_is_enabled (newq)) {
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
     *  new queue has any constraints. As above, queue_requires() is the
     *  effective requires: a job moved into a virtual queue must pick up
     *  the parent's constraint, since that is what makes it schedule as
     *  part of the parent's job list.
     */
    if (queue_requires (newq)) {
        /*  Replace current constraints with those of the new queue
         */
        rc = flux_plugin_arg_pack (args,
                                   FLUX_PLUGIN_ARG_OUT,
                                   "{s:i s:{s:{s:O}}}",
                                   "feasibility", 1,
                                   "updates",
                                    "attributes.system.constraints",
                                     "properties",
                                     queue_requires (newq));
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

json_t *queue_ctx_save (struct queue_ctx *qctx)
{
    return queues_save (qctx->queues);
}

/* Apply post-restore side effects for one queue: if stop_on_restart
 * is set, override a restored started state to stopped with an
 * automated reason (fires notify, which dequeues); otherwise cancel
 * alloc requests for a queue restored in a stopped state.
 */
static int restore_side_effects (struct queue_ctx *qctx, struct queue *q)
{
    if (queue_is_started (q) && qctx->stop_on_restart) {
        if (queue_stop (q,
                        "Automatically stopped due to restart",
                        false) < 0)
            return -1;
    }
    else if (!queue_is_started (q))
        dequeue_jobs (qctx, queue_name (q));
    return 0;
}

static int restore_named_queues (struct queue_ctx *qctx)
{
    zlistx_t *names;
    const char *name;
    struct queue *q;

    if (!(names = queues_list_names (qctx->queues)))
        return -1;
    name = zlistx_first (names);
    while (name) {
        if ((q = queues_lookup (qctx->queues, name, NULL))
            && restore_side_effects (qctx, q) < 0) {
            zlistx_destroy (&names);
            return -1;
        }
        name = zlistx_next (names);
    }
    zlistx_destroy (&names);
    return 0;
}

int queue_ctx_restore (struct queue_ctx *qctx, int version, json_t *o)
{
    /* Apply saved state via queues_restore, which does not fire change
     * notifications, then apply side effects for the resulting state
     * here.
     */
    if (queues_restore (qctx->queues, version, o) < 0)
        return -1;

    if (version == 1) {
        if (!queues_have_named (qctx->queues)) {
            struct queue *q = queues_lookup (qctx->queues, NULL, NULL);
            if (q && restore_side_effects (qctx, q) < 0)
                return -1;
        }
        else if (restore_named_queues (qctx) < 0)
            return -1;
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

    if (!(q = queues_lookup (qctx->queues, name, error))) {
        errno = EINVAL;
        return -1;
    }
    if (!queue_is_enabled (q)) {
        errprintf (error, "job submission%s%s is disabled: %s",
                   name ? " to " : "",
                   name ? name : "",
                   queue_disable_reason (q));
        errno = EINVAL;
        return -1;
    }
    return 0;
}

bool queue_started (struct queue_ctx *qctx, struct job *job)
{
    return queues_queue_is_started (qctx->queues, job->queue);
}

struct queue_ctx *queue_ctx_create (struct job_manager *ctx)
{
    struct queue_ctx *qctx;
    flux_error_t error;

    if (!(qctx = calloc (1, sizeof (*qctx))))
        return NULL;
    qctx->ctx = ctx;
    qctx->stop_on_restart = false;

    if (!(qctx->queues = queues_create ()))
        goto error;
    queues_set_notify (qctx->queues, on_queue_change, qctx);

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
