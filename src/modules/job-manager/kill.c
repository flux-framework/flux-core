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

#include "job.h"
#include "queue.h"
#include "event.h"
#include "kill.h"

#ifndef SIGRTMAX
#  define SIGRTMAX 64
#endif

int kill_check_signal (int signum)
{
    if (signum <= 0 || signum > SIGRTMAX)
        return -1;
    return 0;
}

int kill_allow (uint32_t rolemask, uint32_t userid, uint32_t job_userid)
{
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job_userid)
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

void kill_handle_request (flux_t *h, struct queue *queue,
                          struct event_ctx *event_ctx,
                          const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    int sig;
    flux_future_t *f;
    char topic [64];
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "signum", &sig) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (kill_check_signal (sig) < 0) {
        errstr = "Invalid signal number";
        errno = EINVAL;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id))) {
        errstr = "unknown job id";
        errno = EINVAL;
        goto error;
    }
    if (kill_allow (rolemask, userid, job->userid) < 0) {
        errstr = "guests may only send signals to their own jobs";
        errno = EPERM;
        goto error;
    }
    if (job->state != FLUX_JOB_RUN) {
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

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
