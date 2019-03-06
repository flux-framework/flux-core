/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* priority - adjust job priority
 *
 * Purpose:
 *   Support flux job set-priority command for adjusting job priority
 *   after submission.  Guests can reduce their jobs' priority, or increase
 *   up to the default priority.
 *
 * Input:
 * - job id
 * - new priority
 *
 * Output:
 * - n/a
 *
 * Caveats:
 * - Need to handle case where job has already made request for resources.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "job.h"
#include "queue.h"
#include "util.h"
#include "event.h"
#include "priority.h"

#define MAXOF(a,b)   ((a)>(b)?(a):(b))

struct priority {
    flux_msg_t *request;
    struct job *job;
    int priority;

    struct queue *queue;
    struct event_ctx *event_ctx;
};

static void priority_destroy (struct priority *p)
{
    if (p) {
        int saved_errno = errno;
        flux_msg_destroy (p->request);
        free (p);
        errno = saved_errno;
    }
}

static struct priority *priority_create (struct queue *queue,
                                         struct event_ctx *event_ctx,
                                         struct job *job,
                                         const flux_msg_t *request,
                                         int priority)
{
    struct priority *p;

    if (!(p = calloc (1, sizeof (*p))))
        return NULL;
    p->queue = queue;
    p->event_ctx = event_ctx;
    p->job = job;
    p->priority = priority;
    if (!(p->request = flux_msg_copy (request, false)))
        goto error;
    return p;
error:
    priority_destroy (p);
    return NULL;
}

/* KVS update completed.  Remove job from queue and reinsert.
 */
static void priority_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct priority *p = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        if (flux_respond_error (h, p->request, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }
    p->job->priority = p->priority;
    queue_reorder (p->queue, p->job, p->job->queue_handle);
    if (flux_respond (h, p->request, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
done:
    priority_destroy (p);
}

void priority_handle_request (flux_t *h, struct queue *queue,
                              struct event_ctx *event_ctx,
                              const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    struct priority *p = NULL;
    int priority;
    int rc;
    const char *errstr = NULL;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "priority", &priority) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (priority < FLUX_JOB_PRIORITY_MIN || priority > FLUX_JOB_PRIORITY_MAX) {
        errstr = "priority value is out of range";
        errno = EINVAL;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id))) {
        errstr = "unknown job";
        goto error;
    }
    /* Security: guests can only adjust jobs that they submitted.
     */
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job->userid) {
        errstr = "guests can only reprioritize their own jobs";
        errno = EPERM;
        goto error;
    }
    /* Security: guests can only reduce priority, or increase up to default.
     */
    if (!(rolemask & FLUX_ROLE_OWNER)
            && priority > MAXOF (FLUX_JOB_PRIORITY_DEFAULT, job->priority)) {
        errstr = "guests can only adjust priority <= default";
        errno = EPERM;
        goto error;
    }
    /* TODO: If job has requested resources/exec, don't allow adjustment.
     */
    /* Log KVS event and set KVS priority key asynchronously.
     * Upon successful completion, insert job in new queue position and
     * send response.
     */
    if (!(p = priority_create (queue, event_ctx, job, msg, priority)))
        goto error;
    if (event_log_fmt (p->event_ctx, job, priority_continuation, p,
                       "priority", "userid=%lu priority=%d",
                       (unsigned long)userid, priority) < 0)
        goto error;
    return;
error:
    if (errstr)
        rc = flux_respond_error (h, msg, errno, "%s", errstr);
    else
        rc = flux_respond_error (h, msg, errno, NULL);
    if (rc < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    priority_destroy (p);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
