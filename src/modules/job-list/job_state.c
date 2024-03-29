/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* job_state.c - store information on state of jobs */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <jansson.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libutil/fluid.h"
#include "src/common/libutil/fsd.h"
#include "src/common/libutil/jpath.h"
#include "src/common/libutil/grudgeset.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "src/common/libidset/idset.h"
#include "ccan/str/str.h"

#include "job-list.h"
#include "job_state.h"
#include "job_data.h"
#include "idsync.h"
#include "job_util.h"

#define NUMCMP(a,b) ((a)==(b)?0:((a)<(b)?-1:1))

/* REVERT - flag indicates state transition is a revert, avoid certain
 * checks, clear certain bitmasks on revert
 *
 * CONDITIONAL - flag indicates state transition is dependent on
 * current state.
 */
#define STATE_TRANSITION_FLAG_REVERT 0x1
#define STATE_TRANSITION_FLAG_CONDITIONAL 0x2

typedef enum {
    JOB_UPDATE_TYPE_STATE_TRANSITION,
    JOB_UPDATE_TYPE_JOBSPEC_UPDATE,
    JOB_UPDATE_TYPE_RESOURCE_UPDATE,
} job_update_type_t;

struct job_update {
    job_update_type_t type;

    /* state transitions */
    flux_job_state_t state;
    double timestamp;
    int flags;
    flux_job_state_t expected_state;

    /* jobspec_update, resource_update */
    json_t *update_context;

    /* all updates */
    bool processing;            /* indicates we are waiting for
                                 * current update to complete */
    bool finished;              /* indicates we are done, can remove
                                 * from list */
};

static int submit_context_parse (flux_t *h,
                                 struct job *job,
                                 json_t *context);
static int priority_context_parse (flux_t *h,
                                   struct job *job,
                                   json_t *context);
static int finish_context_parse (flux_t *h,
                                 struct job *job,
                                 json_t *context);
static int urgency_context_parse (flux_t *h,
                                  struct job *job,
                                  json_t *context);
static int exception_context_parse (flux_t *h,
                                    struct job *job,
                                    json_t *context,
                                    int *severityP);
static int dependency_context_parse (flux_t *h,
                                     struct job *job,
                                     const char *cmd,
                                     json_t *context);
static int memo_update (flux_t *h,
                        struct job *job,
                        json_t *context);

static void process_updates (struct job_state_ctx *jsctx, struct job *job);

static int journal_process_events (struct job_state_ctx *jsctx,
                                   const flux_msg_t *msg);

static void update_jobspec (struct job_state_ctx *jsctx,
                            struct job *job,
                            json_t *context,
                            bool update_stats);

/* Compare items for sorting in list, priority first (higher priority
 * before lower priority), job id second N.B. zlistx_comparator_fn signature
 */
static int job_urgency_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;
    int rc;

    if ((rc = (-1)*NUMCMP (j1->priority, j2->priority)) == 0)
        rc = NUMCMP (j1->id, j2->id);
    return rc;
}

/* Compare items for sorting in list by timestamp (note that sorting
 * is in reverse order, most recently (i.e. bigger timestamp)
 * running/completed comes first).  N.B. zlistx_comparator_fn
 * signature
 */
static int job_running_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_run, j1->t_run);
}

static int job_inactive_cmp (const void *a1, const void *a2)
{
    const struct job *j1 = a1;
    const struct job *j2 = a2;

    return NUMCMP (j2->t_inactive, j1->t_inactive);
}

static void job_destroy_wrapper (void **data)
{
    struct job **job = (struct job **)data;
    job_destroy (*job);
}

/* zlistx_insert() and zlistx_reorder() take a 'low_value' parameter
 * which indicates which end of the list to search from.
 * false=search begins at tail (lowest urgency, youngest)
 * true=search begins at head (highest urgency, oldest)
 * Attempt to minimize search distance based on job urgency.
 */
static bool search_direction (struct job *job)
{
    if (job->priority > (FLUX_JOB_PRIORITY_MAX / 2))
        return true;
    else
        return false;
}

static void set_submit_timestamp (struct job *job, double timestamp)
{
    job->t_submit = timestamp;
}

static void update_job_state (struct job_state_ctx *jsctx,
                              struct job *job,
                              flux_job_state_t new_state,
                              double timestamp)
{
    job_stats_update (jsctx->statsctx, job, new_state);

    job->state = new_state;
    if (job->state == FLUX_JOB_STATE_DEPEND)
        job->t_depend = timestamp;
    else if (job->state == FLUX_JOB_STATE_RUN)
        job->t_run = timestamp;
    else if (job->state == FLUX_JOB_STATE_CLEANUP)
        job->t_cleanup = timestamp;
    else if (job->state == FLUX_JOB_STATE_INACTIVE)
        job->t_inactive = timestamp;
    job->states_mask |= job->state;
}

static int job_insert_list (struct job_state_ctx *jsctx,
                            struct job *job,
                            flux_job_state_t newstate)
{
    if (newstate == FLUX_JOB_STATE_DEPEND
        || newstate == FLUX_JOB_STATE_PRIORITY
        || newstate == FLUX_JOB_STATE_SCHED) {
        if (!(job->list_handle = zlistx_insert (jsctx->pending,
                                                job,
                                                search_direction (job))))
            goto enomem;
    }
    else if (newstate == FLUX_JOB_STATE_RUN
             || newstate == FLUX_JOB_STATE_CLEANUP) {
        if (!(job->list_handle = zlistx_insert (jsctx->running, job, true)))
            goto enomem;
    }
    else { /* newstate == FLUX_JOB_STATE_INACTIVE */
        if (!(job->list_handle = zlistx_insert (jsctx->inactive, job, true)))
            goto enomem;
    }

    return 0;

enomem:
    errno = ENOMEM;
    return -1;
}

/* remove job from one list and move it to another based on the
 * newstate */
static void job_change_list (struct job_state_ctx *jsctx,
                             struct job *job,
                             zlistx_t *oldlist,
                             flux_job_state_t newstate)
{
    if (zlistx_detach (oldlist, job->list_handle) < 0)
        flux_log (jsctx->h,
                  LOG_ERR,
                  "%s: zlistx_detach: out of memory",
                  __FUNCTION__);
    job->list_handle = NULL;

    if (job_insert_list (jsctx, job, newstate) < 0)
        flux_log_error (jsctx->h,
                        "error moving job to new list on state transition to %s",
                        flux_job_statetostr (newstate, "L"));
}

static zlistx_t *get_list (struct job_state_ctx *jsctx, flux_job_state_t state)
{
    if (state == FLUX_JOB_STATE_NEW)
        return jsctx->processing;
    else if (state == FLUX_JOB_STATE_DEPEND
             || state == FLUX_JOB_STATE_PRIORITY
             || state == FLUX_JOB_STATE_SCHED)
        return jsctx->pending;
    else if (state == FLUX_JOB_STATE_RUN
             || state == FLUX_JOB_STATE_CLEANUP)
        return jsctx->running;
    else /* state == FLUX_JOB_STATE_INACTIVE */
        return jsctx->inactive;
}

static void update_job_state_and_list (struct job_state_ctx *jsctx,
                                       struct job *job,
                                       flux_job_state_t newstate,
                                       double timestamp)
{
    zlistx_t *oldlist, *newlist;

    oldlist = get_list (jsctx, job->state);
    newlist = get_list (jsctx, newstate);

    /* must call before job_change_list(), to ensure timestamps are
     * set before any sorting based on timestamps are done
     */
    update_job_state (jsctx, job, newstate, timestamp);

    /* when FLUX_JOB_STATE_SCHED is reached, the queue priority has
     * been determined, meaning we can now sort the job on the pending
     * list amongst jobs with queue priorities
     */
    if (oldlist != newlist)
        job_change_list (jsctx, job, oldlist, newstate);
    else if (oldlist == jsctx->pending
             && newstate == FLUX_JOB_STATE_SCHED)
        zlistx_reorder (jsctx->pending,
                        job->list_handle,
                        search_direction (job));

    idsync_check_waiting_id (jsctx->ctx->isctx, job);
}

/* calculate any remaining fields */
static void eventlog_inactive_complete (struct job *job)
{
    /* Default result is failed, overridden below */
    if (job->success)
        job->result = FLUX_JOB_RESULT_COMPLETED;
    else if (job->exception_occurred) {
        if (streq (job->exception_type, "cancel"))
            job->result = FLUX_JOB_RESULT_CANCELED;
        else if (streq (job->exception_type, "timeout"))
            job->result = FLUX_JOB_RESULT_TIMEOUT;
    }
}

static void job_update_destroy (void *data)
{
    struct job_update *updt = data;
    if (updt) {
        int saved_errno = errno;
        json_decref (updt->update_context);
        free (updt);
        errno = saved_errno;
    }
}

static struct job_update *job_update_create (job_update_type_t type)
{
    struct job_update *updt = NULL;

    if (!(updt = calloc (1, sizeof (*updt))))
        return NULL;
    updt->type = type;
    updt->processing = false;
    updt->finished = false;
    return updt;
}

static int append_update (struct job *job, struct job_update *updt)
{
    if (zlist_append (job->updates, updt) < 0) {
        errno = ENOMEM;
        return -1;
    }
    zlist_freefn (job->updates, updt, job_update_destroy, true);
    return 0;
}

static int add_state_transition (struct job *job,
                                 flux_job_state_t newstate,
                                 double timestamp,
                                 int flags,
                                 flux_job_state_t expected_state)
{
    struct job_update *updt = NULL;

    if (!((flags & STATE_TRANSITION_FLAG_REVERT)
          || (flags & STATE_TRANSITION_FLAG_CONDITIONAL))
        && (newstate & job->states_events_mask))
        return 0;

    if (!(updt = job_update_create (JOB_UPDATE_TYPE_STATE_TRANSITION)))
        return -1;

    updt->state = newstate;
    updt->timestamp = timestamp;
    updt->flags = flags;
    updt->expected_state = expected_state;

    if (append_update (job, updt) < 0)
        goto cleanup;

    job->states_events_mask |= newstate;
    return 0;

 cleanup:
    job_update_destroy (updt);
    return -1;
}

static int add_update (struct job *job, json_t *context, job_update_type_t type)
{
    struct job_update *updt = NULL;

    if (!(updt = job_update_create (type)))
        return -1;

    updt->update_context = json_incref (context);

    if (append_update (job, updt) < 0)
        goto cleanup;

    return 0;

 cleanup:
    job_update_destroy (updt);
    return -1;
}

static int add_jobspec_update (struct job *job, json_t *context)
{
    return add_update (job, context, JOB_UPDATE_TYPE_JOBSPEC_UPDATE);
}

static int add_resource_update (struct job *job, json_t *context)
{
    return add_update (job, context, JOB_UPDATE_TYPE_RESOURCE_UPDATE);
}

static void process_state_transition_update (struct job_state_ctx *jsctx,
                                             struct job *job,
                                             struct job_update *updt)
{
    if ((updt->flags & STATE_TRANSITION_FLAG_REVERT)) {
        /* only revert if the current state is what is expected */
        if (job->state == updt->expected_state) {
            job->states_mask &= ~job->state;
            job->states_mask &= ~updt->state;
            update_job_state_and_list (jsctx, job, updt->state, updt->timestamp);
        }
        else {
            updt->finished = true;
            return;
        }
    }
    else if ((updt->flags & STATE_TRANSITION_FLAG_CONDITIONAL)) {
        /* if current state isn't what we expected, move on */
        if (job->state != updt->expected_state) {
            updt->finished = true;
            return;
        }
    }
    if (updt->state == FLUX_JOB_STATE_DEPEND) {
        // process job->jobspec which was obtained from journal
        if (job_parse_jobspec_cached (job, job->jobspec_updates) < 0) {
            flux_log_error (jsctx->h,
                            "%s: error parsing jobspec",
                            idf58 (job->id));
        }
        update_job_state_and_list (jsctx, job, updt->state, updt->timestamp);
        updt->finished = true;
    }
    else if (updt->state == FLUX_JOB_STATE_RUN) {
        // process job->R which was obtained from journal
        if (job_parse_R_cached (job, NULL) < 0) {
            flux_log_error (jsctx->h,
                            "%s: error parsing R",
                            idf58 (job->id));
        }
        update_job_state_and_list (jsctx, job, updt->state, updt->timestamp);
        updt->finished = true;
    }
    else {
        /* FLUX_JOB_STATE_PRIORITY */
        /* FLUX_JOB_STATE_SCHED */
        /* FLUX_JOB_STATE_CLEANUP */
        /* FLUX_JOB_STATE_INACTIVE */

        if (updt->state == FLUX_JOB_STATE_INACTIVE)
            eventlog_inactive_complete (job);

        update_job_state_and_list (jsctx, job, updt->state, updt->timestamp);
        updt->finished = true;
    }
}

static void update_jobspec (struct job_state_ctx *jsctx,
                            struct job *job,
                            json_t *context,
                            bool update_stats)
{
    /* we have not loaded the jobspec yet, save off jobspec updates
     * for an update after jobspec retrieved
     */
    if (!job->jobspec) {
        if (!job->jobspec_updates)
            job->jobspec_updates = json_incref (context);
        else {
            if (json_object_update (job->jobspec_updates, context) < 0)
                flux_log (jsctx->h, LOG_INFO,
                          "%s: job %s failed to update jobspec",
                          __FUNCTION__, idf58 (job->id));
        }
        return;
    }

    /* jobspec-update has the potential to change the job queue,
     * remove the queue specific stats and re-add after the update.
     */
    if (update_stats)
        job_stats_remove_queue (jsctx->statsctx, job);

    job_jobspec_update (job, context);

    if (update_stats)
        job_stats_add_queue (jsctx->statsctx, job);
}

static void process_jobspec_update (struct job_state_ctx *jsctx,
                                    struct job *job,
                                    struct job_update *updt)
{
    /* Generally speaking, after a job is running, jobspec-update
     * events should have no effect.  Note that in some cases,
     * such as job duration, jobspec-updates can alter a job's
     * behavior, but it is via an update to R.  In this case, we
     * elect to not update the job duration seen by the user in
     * the jobspec.  The effect will be seen changes in R (in this
     * example, via the job expiration time in R).
     */
    if (job->state < FLUX_JOB_STATE_RUN)
        update_jobspec (jsctx, job, updt->update_context, true);
    updt->finished = true;
}

static void update_resource (struct job_state_ctx *jsctx,
                             struct job *job,
                             json_t *context)
{
    /* we have not loaded the R yet, save off R updates
     * for an update after jobspec retrieved
     */
    if (!job->R) {
        if (!job->R_updates)
            job->R_updates = json_incref (context);
        else {
            if (json_object_update (job->R_updates, context) < 0)
                flux_log (jsctx->h, LOG_INFO,
                          "%s: job %s failed to update R",
                          __FUNCTION__, idf58 (job->id));
        }
        return;
    }

    job_R_update (job, context);
}

static void process_resource_update (struct job_state_ctx *jsctx,
                                     struct job *job,
                                     struct job_update *updt)
{
    /* Generally speaking, resource-update events only have an effect
     * when a job is running. */
    if (job->state == FLUX_JOB_STATE_RUN)
        update_resource (jsctx, job, updt->update_context);
    updt->finished = true;
}

static void process_updates (struct job_state_ctx *jsctx, struct job *job)
{
    struct job_update *updt;

    while ((updt = zlist_head (job->updates))
           && (!updt->processing || updt->finished)) {

        if (updt->finished)
            goto next;

        if (updt->type == JOB_UPDATE_TYPE_STATE_TRANSITION)
            process_state_transition_update (jsctx, job, updt);
        else if (updt->type == JOB_UPDATE_TYPE_JOBSPEC_UPDATE)
            process_jobspec_update (jsctx, job, updt);
        else /* updt->type == JOB_UPDATE_TYPE_RESOURCE_UPDATE */
            process_resource_update (jsctx, job, updt);

    next:
        if (updt->finished)
            zlist_remove (job->updates, updt);
    }
}

void job_state_pause_cb (flux_t *h, flux_msg_handler_t *mh,
                         const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
    ctx->jsctx->pause = true;

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to pause request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to pause request");
}

void job_state_unpause_cb (flux_t *h, flux_msg_handler_t *mh,
                           const flux_msg_t *msg, void *arg)
{
    struct list_ctx *ctx = arg;
    const flux_msg_t *resp;

    if (!ctx->jsctx->initialized) {
        if (flux_msglist_append (ctx->deferred_requests, msg) < 0)
            goto error;
        return;
    }
    resp = flux_msglist_first (ctx->jsctx->backlog);
    while (resp) {
        if (journal_process_events (ctx->jsctx, resp) < 0)
            goto error;
        flux_msglist_delete (ctx->jsctx->backlog);
        resp = flux_msglist_next (ctx->jsctx->backlog);
    }

    ctx->jsctx->pause = false;

    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "error responding to unpause request");
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "error responding to unpause request");
}

static int job_transition_state (struct job_state_ctx *jsctx,
                                 struct job *job,
                                 flux_job_state_t newstate,
                                 double timestamp,
                                 int flags,
                                 flux_job_state_t expected_state)
{
    if (add_state_transition (job,
                              newstate,
                              timestamp,
                              flags,
                              expected_state) < 0) {
        flux_log_error (jsctx->h, "%s: add_state_transition",
                        __FUNCTION__);
        return -1;
    }
    process_updates (jsctx, job);
    return 0;
}

static int journal_advance_job (struct job_state_ctx *jsctx,
                                struct job *job,
                                flux_job_state_t newstate,
                                double timestamp)
{
    return job_transition_state (jsctx, job, newstate, timestamp, 0, 0);
}

static int journal_revert_job (struct job_state_ctx *jsctx,
                               struct job *job,
                               double timestamp)
{
    /* The flux-restart event is currently only posted to jobs in
     * SCHED state since that is the only state transition defined
     * for the event in RFC21.  In the future, other transitions
     * may be defined.
     */
    return job_transition_state (jsctx,
                                 job,
                                 FLUX_JOB_STATE_PRIORITY,
                                 timestamp,
                                 STATE_TRANSITION_FLAG_REVERT,
                                 FLUX_JOB_STATE_SCHED);
}

static int submit_context_parse (flux_t *h,
                                 struct job *job,
                                 json_t *context)
{
    int urgency;
    int userid;
    int version = -1;

    if (!context
        || json_unpack (context,
                        "{ s:i s:i s?i }",
                        "urgency", &urgency,
                        "userid", &userid,
                        "version", &version) < 0) {
        flux_log (h, LOG_ERR, "%s: submit context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    job->userid = userid;
    job->urgency = urgency;
    job->submit_version = version;
    return 0;
}

static int journal_submit_event (struct job_state_ctx *jsctx,
                                 struct job *job,
                                 flux_jobid_t id,
                                 double timestamp,
                                 json_t *context)
{
    if (!job) {
        if (!(job = job_create (jsctx->h, id)))
            return -1;
        if (zhashx_insert (jsctx->index, &job->id, job) < 0) {
            job_destroy (job);
            errno = EEXIST;
            return -1;
        }
        /* job always starts off on processing list */
        if (!(job->list_handle = zlistx_add_end (jsctx->processing, job))) {
            errno = ENOMEM;
            return -1;
        }
    }

    if (submit_context_parse (jsctx->h, job, context) < 0)
        return -1;
    set_submit_timestamp (job, timestamp);

    return 0;
}

static int priority_context_parse (flux_t *h,
                                   struct job *job,
                                   json_t *context)
{
    if (!context
        || json_unpack (context,
                        "{ s:I }",
                        "priority", (json_int_t *)&job->priority) < 0) {
        flux_log (h, LOG_ERR, "%s: priority context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }
    return 0;
}

static int journal_priority_event (struct job_state_ctx *jsctx,
                                   struct job *job,
                                   double timestamp,
                                   json_t *context)
{
    int64_t orig_priority = job->priority;

    if (priority_context_parse (jsctx->h, job, context) < 0)
        return -1;

    if (job->state & FLUX_JOB_STATE_PENDING
        && job->priority != orig_priority)
        zlistx_reorder (jsctx->pending,
                        job->list_handle,
                        search_direction (job));

    return job_transition_state (jsctx,
                                 job,
                                 FLUX_JOB_STATE_SCHED,
                                 timestamp,
                                 STATE_TRANSITION_FLAG_CONDITIONAL,
                                 FLUX_JOB_STATE_PRIORITY);
}

static int finish_context_parse (flux_t *h,
                                 struct job *job,
                                 json_t *context)
{
    if (!context
        || json_unpack (context,
                        "{ s:i }",
                        "status", &job->wait_status) < 0) {
        flux_log (h, LOG_ERR, "%s: finish context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    /*
     *  A job is successful only if it finished with status == 0
     *  *and* there were no fatal job exceptions:
     */
    if (job->wait_status == 0
        && !(job->exception_occurred && job->exception_severity == 0))
        job->success = true;

    return 0;
}

static int journal_finish_event (struct job_state_ctx *jsctx,
                                 struct job *job,
                                 double timestamp,
                                 json_t *context)
{
    if (finish_context_parse (jsctx->h, job, context) < 0)
        return -1;

    return job_transition_state (jsctx,
                                 job,
                                 FLUX_JOB_STATE_CLEANUP,
                                 timestamp,
                                 0,
                                 0);
}

static int urgency_context_parse (flux_t *h,
                                  struct job *job,
                                  json_t *context)
{
    int urgency;

    if (!context
        || json_unpack (context, "{ s:i }", "urgency", &urgency) < 0
        || (urgency < FLUX_JOB_URGENCY_MIN
            || urgency > FLUX_JOB_URGENCY_MAX)) {
        flux_log (h, LOG_ERR, "%s: urgency context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    job->urgency = urgency;
    return 0;
}

static int journal_urgency_event (struct job_state_ctx *jsctx,
                                  struct job *job,
                                  json_t *context)
{
    return urgency_context_parse (jsctx->h, job, context);
}

static int exception_context_parse (flux_t *h,
                                    struct job *job,
                                    json_t *context,
                                    int *severityP)
{
    const char *type;
    int severity;
    const char *note;

    if (!context
        || json_unpack (context,
                        "{s:s s:i s:s}",
                        "type", &type,
                        "severity", &severity,
                        "note", &note) < 0) {
        flux_log (h, LOG_ERR, "%s: exception context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    if (!job->exception_occurred
        || severity < job->exception_severity) {
        job->exception_occurred = true;
        job->exception_severity = severity;
        job->exception_type = type;
        job->exception_note = note;
        json_decref (job->exception_context);
        job->exception_context = json_incref (context);
    }

    if (severityP)
        (*severityP) = severity;
    return 0;
}


static int dependency_add (struct job *job,
                           const char *description)
{
    if (grudgeset_add (&job->dependencies, description) < 0
        && errno != EEXIST)
        /*  Log non-EEXIST errors, but it is not fatal */
        flux_log_error (job->h,
                        "job %s: dependency-add",
                         idf58 (job->id));
    return 0;
}

static int dependency_remove (struct job *job,
                              const char *description)
{
    int rc = grudgeset_remove (job->dependencies, description);
    if (rc < 0 && errno == ENOENT) {
        /*  No matching dependency is non-fatal error */
        flux_log (job->h,
                  LOG_DEBUG,
                  "job %s: dependency-remove '%s' not found",
                  idf58 (job->id),
                  description);
        rc = 0;
    }
    return rc;
}

static int dependency_context_parse (flux_t *h,
                                     struct job *job,
                                     const char *cmd,
                                     json_t *context)
{
    int rc;
    const char *description = NULL;

    if (!context
        || json_unpack (context,
                        "{s:s}",
                        "description", &description) < 0) {
        flux_log (h, LOG_ERR,
                  "job %s: dependency-%s context invalid",
                  idf58 (job->id),
                  cmd);
        errno = EPROTO;
        return -1;
    }

    if (streq (cmd, "add"))
        rc = dependency_add (job, description);
    else if (streq (cmd, "remove"))
        rc = dependency_remove (job, description);
    else {
        flux_log (h, LOG_ERR,
                  "job %s: invalid dependency event: dependency-%s",
                  idf58 (job->id),
                  cmd);
        return -1;
    }
    return rc;
}

static int memo_update (flux_t *h,
                        struct job *job,
                        json_t *o)
{
    if (!o) {
        flux_log (h, LOG_ERR, "%s: invalid memo context", idf58 (job->id));
        errno = EPROTO;
        return -1;
    }
    if (!job->annotations && !(job->annotations = json_object ())) {
        errno = ENOMEM;
        return -1;
    }
    if (jpath_update (job->annotations, "user", o) < 0
        || jpath_clear_null (job->annotations) < 0)
        return -1;
    if (json_object_size (job->annotations) == 0) {
        json_decref (job->annotations);
        job->annotations = NULL;
    }
    return 0;
}

static int journal_exception_event (struct job_state_ctx *jsctx,
                                    struct job *job,
                                    double timestamp,
                                    json_t *context)
{
    int severity;

    if (exception_context_parse (jsctx->h, job, context, &severity) < 0)
        return -1;

    if (severity == 0)
        return job_transition_state (jsctx,
                                     job,
                                     FLUX_JOB_STATE_CLEANUP,
                                     timestamp,
                                     0,
                                     0);

    return 0;
}

static int journal_annotations_event (struct job_state_ctx *jsctx,
                                      struct job *job,
                                      json_t *context)
{
    json_t *annotations = NULL;

    if (!context
        || json_unpack (context, "{ s:o }", "annotations", &annotations) < 0) {
        flux_log (jsctx->h, LOG_ERR,
                  "%s: annotations event context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }
    json_decref (job->annotations);
    if (json_is_null (annotations))
        job->annotations = NULL;
    else
        job->annotations = json_incref (annotations);

    return 0;
}

static int journal_jobspec_update_event (struct job_state_ctx *jsctx,
                                         struct job *job,
                                         json_t *context)
{
    if (!context) {
        flux_log (jsctx->h, LOG_ERR,
                  "%s: jobspec-update event context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    if (add_jobspec_update (job, context) < 0) {
        flux_log_error (jsctx->h, "%s: add_jobspec_update", __FUNCTION__);
        return -1;
    }
    process_updates (jsctx, job);
    return 0;
}

static int journal_resource_update_event (struct job_state_ctx *jsctx,
                                         struct job *job,
                                         json_t *context)
{
    if (!context) {
        flux_log (jsctx->h, LOG_ERR,
                  "%s: resource-update event context invalid: %s",
                  __FUNCTION__, idf58 (job->id));
        errno = EPROTO;
        return -1;
    }

    if (add_resource_update (job, context) < 0) {
        flux_log_error (jsctx->h, "%s: add_resource_update", __FUNCTION__);
        return -1;
    }
    process_updates (jsctx, job);
    return 0;
}

static int journal_dependency_event (struct job_state_ctx *jsctx,
                                     struct job *job,
                                     const char *cmd,
                                     json_t *context)
{
    return dependency_context_parse (jsctx->h, job, cmd, context);
}

static int journal_process_event (struct job_state_ctx *jsctx,
                                  flux_jobid_t id,
                                  json_t *event,
                                  json_t *jobspec,
                                  json_t *R)
{
    double timestamp;
    const char *name;
    struct job *job;
    json_t *context = NULL;

    if (eventlog_entry_parse (event, &timestamp, &name, &context) < 0) {
        flux_log (jsctx->h, LOG_ERR, "%s: error parsing record",
                  __FUNCTION__);
        errno = EPROTO;
        return -1;
    }

    job = zhashx_lookup (jsctx->index, &id);
    if (job) {
        if (!job->jobspec && jobspec)
            job->jobspec = json_incref (jobspec);
        if (!job->R && R)
            job->R = json_incref (R);
    }

    /* The "submit" event is now posted before the job transitions out of NEW
     * on the "validate" event.  If "invalidate" is posted instead, job
     * submission failed and the job is removed from the KVS.  Drop the
     * nascent job info.
     */
    if (job && streq (name, "invalidate")) {
        if (job->list_handle) {
            zlistx_detach (jsctx->processing, job->list_handle);
            job->list_handle = NULL;
        }
        zhashx_delete (jsctx->index, &job->id);
        /* N.B. since invalid job ids are not released to the submitter, there
         * should be no pending ctx->isctx->lookups requests to clean up here.
         * A test in t2212-job-manager-plugins.t does query invalid ids, but
         * it is careful to ensure that it does so only _after_ the invalidate
         * event has been processed here.
         */
        return 0;
    }

    /*  Job not found is non-fatal, do not return an error.
     *  No need to proceed unless this is the first event (submit),
     *   but log an error since this is an unexpected condition.
     */
    if (!job && !streq (name, "submit")) {
        flux_log (jsctx->h,
                  LOG_ERR,
                  "event %s: job %s not in hash",
                  name,
                  idf58 (id));
        return 0;
    }

    if (streq (name, "submit")) {
        if (journal_submit_event (jsctx,
                                  job,
                                  id,
                                  timestamp,
                                  context) < 0)
            return -1;
    }
    else if (streq (name, "validate")) {
        if (journal_advance_job (jsctx,
                                 job,
                                 FLUX_JOB_STATE_DEPEND,
                                 timestamp) < 0)
            return -1;
    }
    else if (streq (name, "depend")) {
        if (journal_advance_job (jsctx,
                                 job,
                                 FLUX_JOB_STATE_PRIORITY,
                                 timestamp) < 0)
            return -1;
    }
    else if (streq (name, "priority")) {
        if (journal_priority_event (jsctx,
                                    job,
                                    timestamp,
                                    context) < 0)
            return -1;
    }
    else if (streq (name, "alloc")) {
        /* alloc event contains annotations, but we only update
         * annotations via "annotations" events */
        if (journal_advance_job (jsctx,
                                 job,
                                 FLUX_JOB_STATE_RUN,
                                 timestamp) < 0)
            return -1;
    }
    else if (streq (name, "finish")) {
        if (journal_finish_event (jsctx,
                                  job,
                                  timestamp,
                                  context) < 0)
            return -1;
    }
    else if (streq (name, "clean")) {
        if (journal_advance_job (jsctx,
                                 job,
                                 FLUX_JOB_STATE_INACTIVE,
                                 timestamp) < 0)
            return -1;
    }
    else if (streq (name, "urgency")) {
        if (journal_urgency_event (jsctx,
                                   job,
                                   context) < 0)
            return -1;
    }
    else if (streq (name, "exception")) {
        if (journal_exception_event (jsctx,
                                     job,
                                     timestamp,
                                     context) < 0)
            return -1;
    }
    else if (streq (name, "annotations")) {
        if (journal_annotations_event (jsctx,
                                       job,
                                       context) < 0)
            return -1;
    }
    else if (streq (name, "jobspec-update")) {
        if (journal_jobspec_update_event (jsctx, job, context) < 0)
            return -1;
    }
    else if (streq (name, "resource-update")) {
        if (journal_resource_update_event (jsctx, job, context) < 0)
            return -1;
    }
    else if (streq (name, "memo")) {
        if (memo_update (jsctx->h, job, context) < 0)
            return -1;
    }
    else if (strstarts (name, "dependency-")) {
        if (journal_dependency_event (jsctx, job, name+11, context) < 0)
            return -1;
    }
    else if (streq (name, "flux-restart")) {
        /* Presently, job-list depends on job-manager.events-journal
         * service.  So if job-manager reloads, job-list must be
         * reloaded, making the probability of reaching this
         * `flux-restart` path very low.  Code added for completeness
         * and in case dependency removed in the future.
         */
        if (journal_revert_job (jsctx,
                                job,
                                timestamp) < 0)
            return -1;
    }
    return 0;
}

static int journal_process_events (struct job_state_ctx *jsctx,
                                   const flux_msg_t *msg)
{
    flux_jobid_t id;
    json_t *events;
    size_t index;
    json_t *value;
    json_t *jobspec = NULL;
    json_t *R = NULL;

    if (flux_msg_unpack (msg,
                        "{s:I s:o s?o s?o}",
                        "id", &id,
                        "events", &events,
                        "jobspec", &jobspec,
                        "R", &R) < 0)
        return -1;
    if (!json_is_array (events)) {
        errno = EPROTO;
        return -1;
    }
    json_array_foreach (events, index, value) {
        if (journal_process_event (jsctx, id, value, jobspec, R) < 0)
            return -1;
    }

    return 0;
}

static void job_events_journal_continuation (flux_future_t *f, void *arg)
{
    struct job_state_ctx *jsctx = arg;
    const flux_msg_t *msg;
    flux_jobid_t id;

    if (flux_rpc_get_unpack (f, "{s:I}", "id", &id) < 0
        || flux_future_get (f, (const void **)&msg) < 0) {
        if (errno == ENODATA) {
            flux_log (jsctx->h, LOG_INFO, "journal: EOF (exiting)");
            flux_reactor_stop (flux_get_reactor (jsctx->h));
            return;
        }
        flux_log (jsctx->h, LOG_ERR, "journal: %s", future_strerror (f, errno));
        goto error;
    }

    /* Detect sentinel that delimits old and new events.  If there are no
     * more old events, process backlog and begin processing new events
     * as they arrive.
     */
    if (id == FLUX_JOBID_ANY) {
        while ((msg = flux_msglist_pop (jsctx->backlog))) {
            int rc = journal_process_events (jsctx, msg);
            flux_msg_decref (msg);
            if (rc < 0) {
                flux_log_error (jsctx->h, "error processing journal backlog");
                goto error;
            }
        }
        jsctx->initialized = true;
        requeue_deferred_requests (jsctx->ctx);
        flux_future_reset (f);
        return;
    }
    if (!jsctx->initialized || jsctx->pause) {
        if (flux_msglist_append (jsctx->backlog, msg) < 0) {
            flux_log_error (jsctx->h, "error storing journal backlog");
            goto error;
        }
    }
    else {
        if (journal_process_events (jsctx, msg) < 0) {
            flux_log_error (jsctx->h, "error processing events");
            goto error;
        }
    }

    flux_future_reset (f);
    return;

error:
    /* future will be cleaned up in shutdown path */
    flux_reactor_stop_error (flux_get_reactor (jsctx->h));
    return;
}

static flux_future_t *job_events_journal (struct job_state_ctx *jsctx)
{
    flux_future_t *f;

    /* Set full=true so that inactive jobs are included.
     * Don't set allow/deny so that we receive all events.
     */
    if (!(f = flux_rpc_pack (jsctx->h,
                             "job-manager.events-journal",
                             FLUX_NODEID_ANY,
                             FLUX_RPC_STREAMING,
                             "{s:b}",
                             "full", 1))
        || flux_future_then (f,
                             -1,
                             job_events_journal_continuation,
                             jsctx) < 0) {
        flux_log (jsctx->h,
                  LOG_ERR,
                  "error synchronizing with job manager journal: %s",
                  future_strerror (f, errno));
        goto error;
    }
    return f;
error:
    flux_future_destroy (f);
    return NULL;
}

struct job_state_ctx *job_state_create (struct list_ctx *ctx)
{
    struct job_state_ctx *jsctx = NULL;

    if (!(jsctx = calloc (1, sizeof (*jsctx)))) {
        flux_log_error (ctx->h, "calloc");
        return NULL;
    }
    jsctx->h = ctx->h;
    jsctx->ctx = ctx;

    /* Index is the primary data structure holding the job data
     * structures.  It is responsible for destruction.  Lists only
     * contain desired sort of jobs.
     */

    if (!(jsctx->index = job_hash_create ()))
        goto error;
    zhashx_set_destructor (jsctx->index, job_destroy_wrapper);

    if (!(jsctx->pending = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->pending, job_urgency_cmp);

    if (!(jsctx->running = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->running, job_running_cmp);

    if (!(jsctx->inactive = zlistx_new ()))
        goto error;
    zlistx_set_comparator (jsctx->inactive, job_inactive_cmp);

    if (!(jsctx->processing = zlistx_new ()))
        goto error;

    if (!(jsctx->statsctx = job_stats_ctx_create (jsctx->h)))
        goto error;

    if (!(jsctx->backlog = flux_msglist_create ()))
        goto error;

    if (!(jsctx->events = job_events_journal (jsctx)))
        goto error;

    return jsctx;

error:
    job_state_destroy (jsctx);
    return NULL;
}

void job_state_destroy (void *data)
{
    struct job_state_ctx *jsctx = data;
    if (jsctx) {
        int saved_errno = errno;
        /* Destroy index last, as it is the one that will actually
         * destroy the job objects */
        zlistx_destroy (&jsctx->processing);
        zlistx_destroy (&jsctx->inactive);
        zlistx_destroy (&jsctx->running);
        zlistx_destroy (&jsctx->pending);
        zhashx_destroy (&jsctx->index);
        job_stats_ctx_destroy (jsctx->statsctx);
        flux_msglist_destroy (jsctx->backlog);
        flux_future_destroy (jsctx->events);
        free (jsctx);
        errno = saved_errno;
    }
}

int job_state_config_reload (struct job_state_ctx *jsctx,
                             const flux_conf_t *conf,
                             flux_error_t *errp)
{
    return job_stats_config_reload (jsctx->statsctx, conf, errp);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
