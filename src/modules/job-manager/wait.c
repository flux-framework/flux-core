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
 * The event that transitions a job to the CLEANUP state is captured in
 * job->end_event.  RFC 21 dictates it must be a finish event
 * containing a wait(2) style status byte, or a fatal exception.
 * The event is converted to the summary above when the wait response
 * is constructed.
 *
 * If the target job is active when the wait request is received,
 * the request is tacked onto the 'struct job' and processed upon
 * transition to INACTIVE state.  If the target job has already
 * transitioned to INACTIVE, the request is processed immediately.
 *
 * Wait is destructive; that is, job completion info is consumed by
 * the first waiter.  Job completion is spoken for if job->waiter is
 * non-NULL.  An inactive job has already been "reaped" if it is not
 * in the zombie hash.
 *
 * Guests are not permitted to wait on jobs.
 *
 * If the job id is FLUX_JOBID_ANY, then the response is:
 * (1) result of the first inactive zombie
 * (2) result of the next job transitioning to INACTIVE without a waiter
 * (3) ECHILD error if no un-waited-for jobs are available, or there are
 *     no jobs extant that could fulfill the request in the future.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdbool.h>
#include <unistd.h>
#include <sys/types.h>
#include <assert.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"
#include "src/common/libutil/errno_safe.h"
#include "src/common/libutil/errprintf.h"
#include "src/common/libeventlog/eventlog.h"
#include "src/common/libjob/job_hash.h"
#include "src/common/libjob/idf58.h"
#include "ccan/str/str.h"
#include "ccan/ptrint/ptrint.h"

#include "drain.h"
#include "submit.h"
#include "job.h"

struct wait_acct {
    int unreaped_with_waiter;
    int unreaped_no_waiter;
};

struct wait_user {
    uint32_t userid;
    zhashx_t *zombies;
    struct wait_acct acct;
    struct flux_msglist *wait_any_requests;
};

struct waitjob {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
    zhashx_t *users;
};

static const void *userid2key (int userid)
{
    if (userid == 0)
        return int2ptr ((uid_t)-1); // see uid_t note in plugins/history.c
    return int2ptr (userid);
}

static int key2userid (const void *key)
{
    if (key == int2ptr ((uid_t)-1))
        return 0;
    return ptr2int (key);
}

void wait_user_destroy (struct wait_user *user)
{
    if (user) {
        int saved_errno = errno;
        flux_msglist_destroy (user->wait_any_requests);
        zhashx_destroy (&user->zombies);
        free (user);
        errno = saved_errno;
    }
}

static struct wait_user *wait_user_create (uint32_t userid)
{
    struct wait_user *user;

    if (!(user = calloc (1, sizeof (*user))))
        return NULL;
    user->userid = userid;
    if (!(user->wait_any_requests = flux_msglist_create ()))
        goto error;
    if (!(user->zombies = job_hash_create ()))
        goto error;
    zhashx_set_destructor (user->zombies, job_destructor);
    zhashx_set_duplicator (user->zombies, job_duplicator);
    return user;
error:
    wait_user_destroy (user);
    return NULL;
}

static struct wait_user *wait_user_lookup (struct waitjob *wait,
                                           uint32_t userid,
                                           bool create)
{
    struct wait_user *user;

    if (!(user = zhashx_lookup (wait->users, userid2key (userid)))) {
        if (!(user = wait_user_create (userid))
            || zhashx_insert (wait->users, userid2key (userid), user) < 0) {
            wait_user_destroy (user);
            return NULL;
        }
    }
    return user;
}

// zhashx_destructor_fn footprint
static void user_destructor (void **item)
{
    if (item) {
        wait_user_destroy (*item);
        *item = NULL;
    }
}

// zhashx_hash_fn footprint
static size_t userid_hasher (const void *key)
{
    return key2userid (key);
}

// zhashx_comparator_fn footprint
static int userid_comparator (const void *item1, const void *item2)
{
    uint32_t a = key2userid (item1);
    uint32_t b = key2userid (item2);

    if (a == b)
        return 0;
    return (a < b) ? -1 : 1;
}

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
        if (WIFSIGNALED (status)) {
            errprintf (errp,
                       "task(s) %s",
                       strsignal (WTERMSIG (status)));
            *success = false;
        }
        else if (WIFEXITED (status)) {
            errprintf (errp,
                       "task(s) exited with exit code %d",
                       WEXITSTATUS (status));
            *success = WEXITSTATUS (status) == 0 ? true : false;
        }
        else {
            errprintf (errp,
                       "unexpected wait(2) status %d",
                       status);
            *success = false;
        }
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

/* flux_job_wait (FLUX_JOB_ANY) must fail with ECHILD when there is
 * nothing for it to wait on.  Therefore, if the number of available jobs
 * (acct.unreaped_no_waiter) drops below the number of FLUX_JOB_ANY requests
 * one FLUX_JOB_ANY waiter must die.  This may happen if a wait for a specific
 * job is received after a FLUX_JOB_ANY wait.
 */
static void wait_any_fail_one_if_nochild (struct waitjob *wait,
                                          struct wait_user *user)
{
    const flux_msg_t *msg;
    int wait_any_count = flux_msglist_count (user->wait_any_requests);

    if (user->acct.unreaped_no_waiter < wait_any_count
        && !(msg = flux_msglist_last (user->wait_any_requests))) {
        if (flux_respond_error (wait->ctx->h,
                                msg,
                                ECHILD,
                                "there are no more un-waited-for jobs") < 0)
            flux_log_error (wait->ctx->h,
                            "error responding to job-manager.wait request");
        flux_msglist_delete (user->wait_any_requests);
    }
}

/* Callback from process_job_purge().
 */
void wait_notify_inactive_remove (struct waitjob *wait, struct job *job)
{
    struct wait_user *user;

    if ((user = wait_user_lookup (wait, job->userid, false))) {
        if (zhashx_lookup (user->zombies, &job->id)) {
            zhashx_delete (user->zombies, &job->id);
            user->acct.unreaped_no_waiter--;
        }
    }
}

/* Callback from event_job_action().  The 'job' has entered INACTIVE state.
 * If it has not already been reaped, respond to a pending waiter or
 * add it to the user's zombie hash.
 */
void wait_notify_inactive (struct waitjob *wait, struct job *job)
{
    const flux_msg_t *req;
    struct wait_user *user;

    if (!(user = wait_user_lookup (wait, job->userid, false)))
        return;

    if (job->waiter) {
        wait_respond (wait, job->waiter, job);
        flux_msg_decref (job->waiter);
        job->waiter = NULL;
        user->acct.unreaped_with_waiter--;
    }
    else if ((req = flux_msglist_first (user->wait_any_requests))) {
        wait_respond (wait, req, job);
        flux_msglist_delete (user->wait_any_requests);
        user->acct.unreaped_no_waiter--;
    }
    else
        zhashx_update (user->zombies, &job->id, job);
}

/* Callback from submit.c and restart.c where ctx->active_jobs is increased.
 * Lookup/create a user context for this user update counts.
 */
void wait_notify_active (struct waitjob *wait, struct job *job)
{
    struct wait_user *user;

    if ((user = wait_user_lookup (wait, job->userid, true)))
        user->acct.unreaped_no_waiter++;
}

static int wait_rpc_any (struct waitjob *wait,
                         struct wait_user *user,
                         const flux_msg_t *msg,
                         flux_error_t *error)
{
    struct job *job;
    int wait_any_count = flux_msglist_count (user->wait_any_requests);

    if (user->acct.unreaped_no_waiter == wait_any_count) {
        errprintf (error, "there are no un-waited-for jobs");
        errno = ECHILD;
        return -1;
    }
    /* If there's a zombie, reap it.
     */
    if ((job = zhashx_first (user->zombies))) {
        wait_respond (wait, msg, job);
        user->acct.unreaped_no_waiter--;
        zhashx_delete (user->zombies, &job->id);
    }
    /* Enqueue request until a job becomes a zombie.
     */
    else if (flux_msglist_append (user->wait_any_requests, msg) < 0) {
        errprintf (error,
                   "could not enqueue wait request: %s",
                   strerror (errno));
        return -1;
    }
    return 0;
}

static int wait_rpc_one (struct waitjob *wait,
                         struct wait_user *user,
                         const flux_msg_t *msg,
                         flux_jobid_t id,
                         flux_error_t *error)
{
    struct job *job;
    struct job_manager *ctx = wait->ctx;

    /* If job is already a zombie, reap it.
    */
    if ((job = zhashx_lookup (user->zombies, &id))) {
        wait_respond (ctx->wait, msg, job);
        user->acct.unreaped_no_waiter--;
        zhashx_delete (user->zombies, &job->id);
        wait_any_fail_one_if_nochild (wait, user);
    }
    /* If job is still active, enqueue the request.
     */
    else if ((job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (job->waiter) {
            errprintf (error, "job id already has a waiter");
            errno = ECHILD;
            return -1;
        }
        if (user->userid != job->userid) {
            errprintf (error, "job id does not belong to you");
            errno = EPERM;
            return -1;
        }
        job->waiter = flux_msg_incref (msg);
        user->acct.unreaped_no_waiter--;
        user->acct.unreaped_with_waiter++;
        wait_any_fail_one_if_nochild (wait, user);
    }
    /* Try to give an informative error if neither of those things worked.
     */
    else if ((job = zhashx_lookup (ctx->inactive_jobs, &id))) {
        if (user->userid != job->userid) {
            errprintf (error, "job id does not belong to you");
            errno = EPERM;
            return -1;
        }
        else {
            errprintf (error, "job id was already waited on");
            errno = ECHILD;
            return -1;
        }
    }
    else {
        errprintf (error, "invalid job id");
        errno = ECHILD;
        return -1;
    }
    return 0;
}

static void wait_rpc (flux_t *h,
                      flux_msg_handler_t *mh,
                      const flux_msg_t *msg,
                      void *arg)
{
    struct job_manager *ctx = arg;
    struct flux_msg_cred cred;
    flux_jobid_t id;
    struct wait_user *user;
    flux_error_t error;

    if (flux_request_unpack (msg, NULL, "{s:I}", "id", &id) < 0
        || flux_msg_get_cred (msg, &cred) < 0) {
        errprintf (&error, "malformed wait request");
        goto error;
    }
    if (!(user = wait_user_lookup (ctx->wait, cred.userid, false))) {
        errno = EINVAL;
        errprintf (&error, "unknown user");
        goto error;
    }
    if (id == FLUX_JOBID_ANY) {
        if (wait_rpc_any (ctx->wait, user, msg, &error) < 0)
            goto error;
    }
    else {
        if (wait_rpc_one (ctx->wait, user, msg, id, &error) < 0)
            goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, error.text) < 0)
        flux_log_error (h, "error responding to wait request");
}

/* A client has disconnected.  Destroy any acct.unreaped_with_waiter registered by that client.
 */
void wait_disconnect_rpc (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_manager *ctx = arg;
    struct waitjob *wait = ctx->wait;
    struct job *job;
    struct flux_msg_cred cred;
    struct wait_user *user;

    if (flux_msg_get_cred (msg, &cred) < 0
        || !(user = wait_user_lookup (wait, cred.userid, false)))
        return;

    if (user->acct.unreaped_with_waiter > 0) {
        job = zhashx_first (ctx->active_jobs);
        while (job) {
            if (job->waiter && cred.userid == job->userid) {
                if (flux_msg_route_match_first (job->waiter, msg)) {
                    flux_msg_decref (job->waiter);
                    job->waiter = NULL;
                    user->acct.unreaped_no_waiter++;
                    user->acct.unreaped_with_waiter--;
                    if (user->acct.unreaped_with_waiter == 0)
                        break;
                }
            }
            job = zhashx_next (ctx->active_jobs);
        }
    }
    flux_msglist_disconnect (user->wait_any_requests, msg);
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
         */
        job = zhashx_first (wait->ctx->active_jobs);
        while (job) {
            if (job->waiter) {
                respond_unloading (h, job->waiter);
                flux_msg_decref (job->waiter);
                job->waiter = NULL;
            }
            job = zhashx_next (wait->ctx->active_jobs);
        }
        if (wait->users) {
            struct wait_user *user;
            const flux_msg_t *msg;

            user = zhashx_first (wait->users);
            while (user) {
                while ((msg = flux_msglist_first (user->wait_any_requests))) {
                    respond_unloading (h, msg);
                    flux_msglist_delete (user->wait_any_requests);
                    msg = flux_msglist_next (user->wait_any_requests);
                }
                user = zhashx_next (wait->users);
            }
            zhashx_destroy (&wait->users);
        }

        free (wait);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        .typemask = FLUX_MSGTYPE_REQUEST,
        .topic_glob = "job-manager.wait",
        .cb = wait_rpc,
        .rolemask = FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct waitjob *wait_ctx_create (struct job_manager *ctx)
{
    struct waitjob *wait;

    if (!(wait = calloc (1, sizeof (*wait))))
        return NULL;
    wait->ctx = ctx;
    if (!(wait->users = zhashx_new ()))
        goto nomem;
    zhashx_set_destructor (wait->users, user_destructor);
    zhashx_set_key_hasher (wait->users, userid_hasher);
    zhashx_set_key_comparator (wait->users, userid_comparator);
    zhashx_set_key_destructor (wait->users, NULL);
    zhashx_set_key_duplicator (wait->users, NULL);
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &wait->handlers) < 0)
        goto error;
    return wait;
nomem:
    errno = ENOMEM;
error:
    wait_ctx_destroy (wait);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
