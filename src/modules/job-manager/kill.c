/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* kill - send a signal to a running job
 *
 * Purpose:
 *   Handle job-manager.kill RPC
 *
 * Input:
 * - job id, signum
 *
 * Action:
 * - check for valid job and job state
 * - broadcast kill event for job shells
 *
 * Caveats:
 * - kill event is open loop and may not be delivered to all job shells
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <signal.h>
#include <flux/core.h>

#include "src/common/libczmqcontainers/czmq_containers.h"

#include "job.h"
#include "event.h"
#include "kill.h"
#include "job-manager.h"

struct kill {
    struct job_manager *ctx;
    flux_msg_handler_t **handlers;
};

int kill_check_signal (int signum)
{
    if (signum <= 0 || signum >= NSIG)
        return -1;
    return 0;
}

static int kill_event_topic_str (char *s, size_t len, flux_jobid_t id)
{
    int n = snprintf (s, len, "shell-%ju.kill", (uintmax_t) id);
    if (n < 0 || n >= len)
        return -1;
    return 0;
}

void kill_handle_request (flux_t *h,
                          flux_msg_handler_t *mh,
                          const flux_msg_t *msg,
                          void *arg)
{
    struct job_manager *ctx = arg;
    flux_jobid_t id;
    struct job *job;
    int sig;
    flux_future_t *f;
    char topic [64];
    const char *errstr = NULL;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:I s:i}",
                             "id", &id,
                             "signum", &sig) < 0)
        goto error;
    if (kill_check_signal (sig) < 0) {
        errstr = "Invalid signal number";
        errno = EINVAL;
        goto error;
    }
    if (!(job = zhashx_lookup (ctx->active_jobs, &id))) {
        if (!(job = zhashx_lookup (ctx->inactive_jobs, &id)))
            errstr = "unknown job id";
        else
            errstr = "job is inactive";
        errno = EINVAL;
        goto error;
    }
    if (flux_msg_authorize (msg, job->userid) < 0) {
        errstr = "guests may only send signals to their own jobs";
        goto error;
    }
    if (!(job->state & FLUX_JOB_STATE_RUNNING)) {
        errstr = "job is not running";
        errno = EINVAL;
        goto error;
    }
    if (kill_event_topic_str (topic, sizeof (topic), id) < 0) {
        errstr = "internal error creating event topic string";
        goto error;
    }
    if (!(f = flux_event_publish_pack (h, topic, 0, "{s:i}", "signum", sig)))
        goto error;
    flux_future_destroy (f);
    if (flux_respond (h, msg, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/* Send a signal to all jobs belonging to 'userid'.
 * Consider userid == FLUX_USERID_UNKNOWN to be a wildcard matching all users.
 */
void killall_handle_request (flux_t *h,
                             flux_msg_handler_t *mh,
                             const flux_msg_t *msg,
                             void *arg)
{
    struct job_manager *ctx = arg;
    uint32_t userid;
    int signum;
    const char *errstr = NULL;
    char topic [64];
    struct job *job;
    flux_future_t *f;
    int dry_run;
    int count = 0;
    int error_count = 0;

    if (flux_request_unpack (msg,
                             NULL,
                             "{s:b s:i s:i}",
                             "dry_run", &dry_run,
                             "userid", &userid,
                             "signum", &signum) < 0) {
        errstr = "error decoding request";
        goto error;
    }
    /* Only the instance owner gets to use the userid wildcard.
     * Guests must specify 'userid' = themselves.
     */
    if (flux_msg_authorize (msg, userid) < 0) {
        errstr = "guests can only kill their own jobs";
        goto error;
    }
    if (kill_check_signal (signum) < 0) {
        errstr = "Invalid signal number";
        errno = EINVAL;
        goto error;
    }
    job = zhashx_first (ctx->active_jobs);
    while (job) {
        if (!(job->state & FLUX_JOB_STATE_RUNNING))
            goto next;
        if (userid != FLUX_USERID_UNKNOWN && userid != job->userid)
            goto next;
        count++;
        if (!dry_run) {
            if (kill_event_topic_str (topic, sizeof (topic), job->id) < 0) {
                error_count++;
                goto next;
            }
            if (!(f = flux_event_publish_pack (h,
                                               topic,
                                               0,
                                               "{s:i}",
                                               "signum", signum))) {
                error_count++;
                goto next;
            }
            flux_future_destroy (f);
        }
next:
        job = zhashx_next (ctx->active_jobs);
    }
    if (flux_respond_pack (h,
                           msg,
                           "{s:i s:i}",
                           "count", count,
                           "errors", error_count) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
    return;
error:
    if (flux_respond_error (h, msg, errno, errstr) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}


void kill_ctx_destroy (struct kill *kill)
{
    if (kill) {
        int saved_errno = errno;
        flux_msg_handler_delvec (kill->handlers);
        free (kill);
        errno = saved_errno;
    }
}

static const struct flux_msg_handler_spec htab[] = {
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.kill",
        kill_handle_request,
        FLUX_ROLE_USER
    },
    {
        FLUX_MSGTYPE_REQUEST,
        "job-manager.killall",
        killall_handle_request,
        FLUX_ROLE_USER
    },
    FLUX_MSGHANDLER_TABLE_END,
};

struct kill *kill_ctx_create (struct job_manager *ctx)
{
    struct kill *kill;

    if (!(kill = calloc (1, sizeof (*kill))))
        return NULL;
    kill->ctx = ctx;
    if (flux_msg_handler_addvec (ctx->h, htab, ctx, &kill->handlers) < 0)
        goto error;
    return kill;
error:
    kill_ctx_destroy (kill);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
