/************************************************************\
 * Copyright 2019 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* wait.c - request a job's exit status
 *
 * Handle flux_job_wait (id) requests.
 *
 * The call blocks until the job transitions to INACTIVE, then
 * a summary of the job result is returned:
 * - a boolean success
 * - a textual error string
 *
 * The event that transitions a waitable job to the CLEANUP state is
 * captured in job->end_event.  RFC 21 dictates it must be a finish event
 * containing a wait(2) style status byte, or a fatal exception.
 * The event is converted to the summary above when the wait response
 * is constructed.
 *
 * If the target job is active when the wait request is received,
 * the request is tacked onto the 'struct job' and processed upon
 * transition to INACTIVE state.  If the target waitable job has already
 * transitioned to INACTIVE, it is found in the wait->zombies hash
 * and the request is processed immediately.
 *
 * Only jobs submitted with the FLUX_JOB_WAITABLE can be waited on.
 *
 * Wait is destructive; that is, job completion info is consumed by
 * the first waiter.
 *
 * Guests are not permitted to wait on jobs or set FLUX_JOB_WAITABLE,
 * to avoid possible unchecked zombie growth in a system instance.
 *
 * If the job id is FLUX_JOBID_ANY, then the response is:
 * (1) result of the first job found in the wait->zombies hash
 * (2) result of the next waitable job transitioning to INACTIVE,
 *     without a waiter on the specific ID
 * (3) ECHILD error if no waitable jobs are available, or there are
 *     more waiters than jobs
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"

#include "drain.h"
#include "submit.h"
#include "job.h"

struct waitjob {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zhashx_t *zombies;
    int waiters; // count of waiters blocked on specific active jobs
    int waitables; // count of active waitable jobs
    struct flux_msglist *requests; // requests to wait in FLUX_JOBID_ANY
};

static int decode_job_result (struct job *job,
                              bool *success,
                              flux_error_t *errp)
{
    const char *name;
    json_t *context;

    if (!job->end_event)
        return -1;
    if (eventlog_entry_parse (job->end_event, NULL, &name, &context) < 0)
        return -1;

    /* Exception - set errbuf=description, set success=false
     */
    if (streq (name, "exception")) {
        const char *type;
        const char *note;

        if (json_unpack (context,
                         "{s:s s:s}",
                         "type", &type,
                         "note", &note) < 0)
            return -1;
        errprintf (errp,
                   "Fatal exception type=%s %s",
                   type,
                   note);
        *success = false;
    }
    /* Shells exited - set errbuf=decoded status byte,
     * set success=true if all shells exited with 0, otherwise false.
     */
    else if (streq (name, "finish")) {
        int status;
        if (json_unpack (context, "{s:i}", "status", &status) < 0)
            return -1;
        if (flux_job_waitstatus_to_exitcode (status, errp) != 0)
            *success = false;
        else
            *success = true;
    }
    else
        return -1;
    return 0;
}

/* Respond to wait request 'msg' with completion info from 'job'.
 */
static void wait_respond (struct waitjob *wait,
                          const flux_msg_t *msg,
                          struct job *job)
{
    flux_t *h = wait->ctx->h;
    flux_error_t error;
    bool success;

    if (decode_job_result (job, &success, &error) < 0) {
        flux_log (h,
                  LOG_ERR,
                  "wait_respond id=%s: result decode failure",
                  idf58 (job->id));
        goto error;
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:I s:b s:s}",
                           "id", job->id,
                           "success", success ? 1 : 0,
                           "errstr", error.text) < 0)
        flux_log_error (h, "wait_respond id=%s", idf58 (job->id));
    return;
error:
    if (flux_respond_error (h, msg, errno, "Flux job wait internal error") < 0)
        flux_log_error (h, "wait_respond id=%s", idf58 (job->id));
}

/* Callback from event_job_action().  The 'job' has entered INACTIVE state.
 * Respond to a pending waiter, if any.  Otherwise insert into zombies
 * hash for a future wait request.
 */
void wait_notify_inactive (struct waitjob *wait, struct job *job)
{
    flux_t *h = wait->ctx->h;
    const flux_msg_t *req;

    assert ((job->flags & FLUX_JOB_WAITABLE));

    if (job->waiter) {
        wait_respond (wait, job->waiter, job);
        flux_msg_decref (job->waiter);
        job->waiter = NULL;
        wait->waiters--;
    }
    else if ((req = flux_msglist_first (wait->requests))) {
        wait_respond (wait, req, job);
        flux_msglist_delete (wait->requests);
    }
    else {
        if (zhashx_insert (wait->zombies, &job->id, job) < 0) // increfs job
            flux_log (h, LOG_ERR, "zhashx_insert into zombies hash failed");
    }
    wait->waitables--;
}

/* Callback from submit.c and restart.c where ctx->active_jobs is increased.
 * Maintain count of waitable jobs.
 */
void wait_notify_active (struct waitjob *wait, struct job *job)
{
    assert ((job->flags & FLUX_JOB_WAITABLE));
    wait->waitables++;
}

static void wait_rpc (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_manager *ctx = arg;
    struct waitjob *wait = ctx->wait;
    flux_jobid_t id;
    struct job *job;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0) {
        errstr = "malformed wait request";
        goto error;
    }
    if (id == FLUX_JOBID_ANY) {
        /* If there's a zombie, respond and destroy it.
         */
        if ((job = zhashx_first (wait->zombies))) {
            wait_respond (wait, msg, job);
            zhashx_delete (wait->zombies, &job->id);
        }
        /* Enqueue request until a waitable job transitions to inactive.
         */
        else {
            if (flux_msglist_append (wait->requests, msg) < 0)
                goto error;
        }
    }
    else {
        /* If job is already a zombie, respond and destroy zombie. Done!
         */
        if ((job = zhashx_lookup (wait->zombies, &id))) {
            wait_respond (wait, msg, job);
            zhashx_delete (wait->zombies, &id); // decrefs job
        }
        /* If job is still active, enqueue the request.
         */
        else if ((job = zhashx_lookup (ctx->active_jobs, &id))) {
            if (job->waiter) {
                errstr = "job id already has a waiter";
                goto error_nojob;
            }
            if (!(job->flags & FLUX_JOB_WAITABLE)) {
                errstr = "job was not submitted with FLUX_JOB_WAITABLE";
                goto error_nojob;
            }
            job->waiter = flux_msg_incref (msg);
            wait->waiters++;
            return;
        }
        /* Invalid jobid, not waitable, or already waited on.
         */
        else {
            errstr = "invalid job id, or job may be inactive and not waitable";
            goto error_nojob;
        }
    }
    /* Ensure that the action taken above does not result in more waiters than
     * waitables.  Fail the most recently added FLUX_JOBID_ANY waiter if so.
     * This could be due to
     * (1) wait on specific ID increased wait->waiters, or
     * (2) wait on FLUX_JOBID_ANY increased wait->requests.
     */
    if (flux_msglist_count (wait->requests) + wait->waiters > wait->waitables) {
        const flux_msg_t *req = flux_msglist_last (wait->requests);

        if (req) {
            if (flux_respond_error (h,
                                    req,
                                    ECHILD,
                                    "there are no more waitable jobs") < 0)
                flux_log_error (h, "%s: flux_respond_error", __func__);
            flux_msglist_delete (wait->requests);
        }
    }
    return;

error_nojob:
    errno = ECHILD; // mimic wait(2)
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __func__);
}

/* A client has disconnected.  Destroy any waiters registered by that client.
 */
void wait_disconnect_rpc (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_manager *ctx = arg;
    struct waitjob *wait = ctx->wait;
    struct job *job;

    job = zhashx_first (ctx->active_jobs);
    while (job && wait->waiters > 0) {
        if (job->waiter) {
            if (flux_msg_route_match_first (job->waiter, msg)) {
                flux_msg_decref (job->waiter);
                job->waiter = NULL;
                wait->waiters--;
            }
        }
        job = zhashx_next (ctx->active_jobs);
    }

    flux_msglist_disconnect (wait->requests, msg);
}

struct job *wait_zombie_first (struct waitjob *wait)
{
    return zhashx_first (wait->zombies);
}

struct job *wait_zombie_next (struct waitjob *wait)
{
    return zhashx_next (wait->zombies);
}

static void respond_unloading (flux_t *h, const flux_msg_t *msg)
{
    if (flux_respond_error (h, msg, ENOSYS, "job-manager is unloading") < 0)
        flux_log_error (h, "respond failed in wait teardown");
}

void wait_ctx_destroy (struct waitjob *wait)
{
    if (wait) {
        flux_t *h = wait->ctx->h;
        struct job *job;

        int saved_errno = errno;
        flux_msg_handler_delvec (wait->handlers);

        /* Iterate through active jobs, sending ENOSYS response to
         * any pending wait requests, indicating that the module is unloading.
         * Use wait->waiters count to avoid unnecessary scanning.
         */
        job = zhashx_first (wait->ctx->active_jobs);
        while (job && wait->waiters > 0) {
            if (job->waiter) {
                respond_unloading (h, job->waiter);
                flux_msg_decref (job->waiter);
                job->waiter = NULL;
                wait->waiters--;
            }
            job = zhashx_next (wait->ctx->active_jobs);
        }

        /* Send ENOSYS to any pending FLUX_JOBID_ANY wait requests,
         * indicating that the module is unloading.
         */
        if (wait->requests) {
            const flux_msg_t *msg;

            while ((msg = flux_msglist_first (wait->requests))) {
                respond_unloading (h, msg);
                flux_msglist_delete (wait->requests);
                msg = flux_msglist_next (wait->requests);
            }
            flux_msglist_destroy (wait->requests);
        }

        zhashx_destroy (&wait->zombies);
        free (wait);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "job-manager.wait",
        .cb = wait_rpc,
        .rolemask = 0
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct waitjob *wait_ctx_create (struct job_manager *ctx)
{
    struct waitjob *wait;

    if (!(wait = calloc (1, sizeof (*wait))))
        return NULL;
    wait->ctx = ctx;

    if (!(wait->zombies = job_hash_create ()))
        goto error;
    zhashx_set_destructor (wait->zombies, job_destructor);
    zhashx_set_duplicator (wait->zombies, job_duplicator);

    if (!(wait->requests = flux_msglist_create ()))
        goto error;

    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &wait->handlers) < 0)
        goto error;
    return wait;
error:
    wait_ctx_destroy (wait);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
