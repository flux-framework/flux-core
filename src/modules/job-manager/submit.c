/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* submit.c - handle job-manager.submit request from job-ingest */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <czmq.h>
#include <jansson.h>
#include <flux/core.h>

#include "job.h"
#include "alloc.h"
#include "event.h"
#include "journal.h"
#include "wait.h"

#include "submit.h"

#include "src/common/libeventlog/eventlog.h"

struct submit {
    struct job_manager *ctx;
    bool submit_disable;
    char *disable_errmsg;
    flux_msg_handler_t **handlers;
};

/* Decode 'o' into a struct job, then add it to the active_job hash.
 * Also record the job in 'newjobs'.
 */
int submit_add_one_job (zhashx_t *active_jobs, zlist_t *newjobs, json_t *o)
{
    struct job *job;

    if (!(job = job_create ()))
        return -1;
    if (json_unpack (o, "{s:I s:i s:i s:f s:i s:O}",
                        "id", &job->id,
                        "urgency", &job->urgency,
                        "userid", &job->userid,
                        "t_submit", &job->t_submit,
                        "flags", &job->flags,
                        "jobspec", &job->jobspec_redacted) < 0) {
        errno = EPROTO;
        job_decref (job);
        return -1;
    }
    if (zhashx_insert (active_jobs, &job->id, job) < 0) {
        job_decref (job);
        /* zhashx_insert() fails if hash item already exists.
         * This is not an error - there is a window for restart_from_kvs()
         * to pick up a job that also has a submit request in flight.
         */
        return 0;
    }
    if (zlist_push (newjobs, job) < 0) {
        zhashx_delete (active_jobs, &job->id);
        job_decref (job);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* The submit request has failed.  Dequeue jobs recorded in 'newjobs',
 * then destroy the newjobs list.
 */
void submit_add_jobs_cleanup (zhashx_t *active_jobs, zlist_t *newjobs)
{
    if (newjobs) {
        int saved_errno = errno;
        struct job *job;
        while ((job = zlist_pop (newjobs))) {
            zhashx_delete (active_jobs, &job->id);
            job_decref (job);
        }
        zlist_destroy (&newjobs);
        errno = saved_errno;
    }
}

/* Add jobs from 'jobs' array to 'active_jobs' hash.
 * On success, return a list of struct job's.
 * On failure, return NULL with errno set (no jobs added).
 */
zlist_t *submit_add_jobs (zhashx_t *active_jobs, json_t *jobs)
{
    size_t index;
    json_t *el;
    zlist_t *newjobs;

    if (!(newjobs = zlist_new ())) {
        errno = ENOMEM;
        return NULL;
    }
    json_array_foreach (jobs, index, el) {
        if (submit_add_one_job (active_jobs, newjobs, el) < 0)
            goto error;
    }
    return newjobs;
error:
    submit_add_jobs_cleanup (active_jobs, newjobs);
    return NULL;
}

/* Submit event requires special handling.  It cannot go through
 * event_job_post_pack() because job-ingest already logged it.
 * However, we want to let the state machine choose the next state and action,
 * We instead re-create the event and run it directly through
 * event_job_update() and event_job_action().
 */
static int submit_post_event (struct job_manager *ctx, struct job *job)
{
    json_t *entry = NULL;
    int rv = -1;

    entry = eventlog_entry_pack (job->t_submit,
                                 "submit",
                                 "{ s:i s:i s:i }",
                                 "userid", job->userid,
                                 "urgency", job->urgency,
                                 "flags", job->flags);
    if (!entry)
        goto error;
    /* call before eventlog_seq increment below */
    if (journal_process_event (ctx->journal,
                               job->id,
                               job->eventlog_seq,
                               "submit",
                               entry) < 0)
        goto error;
    if (event_job_update (job, entry) < 0) /* NEW -> DEPEND */
        goto error;
    job->eventlog_seq++;
    if (event_batch_pub_state (ctx->event, job, job->t_submit) < 0)
        goto error;
    if (event_job_action (ctx->event, job) < 0)
        goto error;
    rv = 0;
 error:
    json_decref (entry);
    return rv;
}

/* handle submit request (from job-ingest module)
 * This is a batched request for one or more jobs already validated
 * by the ingest module, and already instantiated in the KVS.
 * The user isn't handed the jobid though until we accept the job here.
 */
static void submit_cb (flux_t *h, flux_msg_handler_t *mh,
                       const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    json_t *jobs;
    zlist_t *newjobs;
    struct job *job;
    const char *errmsg = NULL;

    if (flux_request_unpack (msg, NULL, "{s:o}", "jobs", &jobs) < 0) {
        flux_log_error (h, "%s", __FUNCTION__);
        goto error;
    }
    if (ctx->submit->submit_disable) {
        errno = EINVAL;
        errmsg = ctx->submit->disable_errmsg;
        goto error;
    }
    if (!(newjobs = submit_add_jobs (ctx->active_jobs, jobs))) {
        flux_log_error (h, "%s: error enqueuing batch", __FUNCTION__);
        goto error;
    }
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);

    /* Submitting user is being responded to with jobid's.
     * Now walk the list of new jobs and advance their state.
     * Side effect: update ctx->max_jobid.
     */
    while ((job = zlist_pop (newjobs))) {
        if (submit_post_event (ctx, job) < 0)
            flux_log_error (h, "%s: submit_post_event id=%ju",
                            __FUNCTION__, (uintmax_t)job->id);

        if ((job->flags & FLUX_JOB_WAITABLE))
            wait_notify_active (ctx->wait, job);
        if (ctx->max_jobid < job->id)
            ctx->max_jobid = job->id;

        job_decref (job);
    }
    zlist_destroy (&newjobs);
    return;
error:
    if (flux_respond_error (h, msg, errno, errmsg) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

static void submit_admin_cb (flux_t *h, flux_msg_handler_t *mh,
                             const flux_msg_t *msg, void *arg)
{
    struct job_manager *ctx = arg;
    const char *error_prefix = "job submission is disabled: ";
    int enable;
    int query_only;
    const char *reason;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s:b s:s}",
                             "query_only",
                             &query_only,
                             "enable",
                             &enable,
                             "reason",
                             &reason) < 0)
        goto error;
    if (!query_only) {
        if (!enable) {
            char *errmsg;
            if (asprintf (&errmsg, "%s%s", error_prefix, reason) < 0)
                goto error;
            free (ctx->submit->disable_errmsg);
            ctx->submit->disable_errmsg = errmsg;
        }
        ctx->submit->submit_disable = enable ? false : true;
    }
    if (ctx->submit->submit_disable)
        reason = ctx->submit->disable_errmsg + strlen (error_prefix);
    if (flux_respond_pack (h,
                           msg,
                           "{s:b s:s}",
                           "enable",
                           ctx->submit->submit_disable ? 0 : 1,
                           "reason",
                           ctx->submit->submit_disable ? reason : "") < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

void submit_ctx_destroy (struct submit *submit)
{
    if (submit) {
        int saved_errno = errno;
        flux_msg_handler_delvec (submit->handlers);
        free (submit->disable_errmsg);
        free (submit);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    { FLUX_MSGTYPE_REQUEST, "job-manager.submit", submit_cb, 0},
    { FLUX_MSGTYPE_REQUEST, "job-manager.submit-admin", submit_admin_cb, 0},
    FLUX_MSGHANDLER_TABLE_END,
};

struct submit *submit_ctx_create (struct job_manager *ctx)
{
    struct submit *submit;

    if (!(submit = calloc (1, sizeof (*submit))))
        return NULL;
    submit->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &submit->handlers) < 0)
        goto error;
    return submit;
error:
    submit_ctx_destroy (submit);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
