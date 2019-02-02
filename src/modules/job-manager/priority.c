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

#include "src/common/libjob/job.h"

#include "job.h"
#include "queue.h"
#include "active.h"
#include "priority.h"

#define MAXOF(a,b)   ((a)>(b)?(a):(b))

struct priority {
    flux_msg_t *request;
    struct job *job;
    flux_kvs_txn_t *txn;
    int priority;

    struct queue *queue;
};

static void priority_destroy (struct priority *p)
{
    if (p) {
        int saved_errno = errno;
        flux_msg_destroy (p->request);
        flux_kvs_txn_destroy (p->txn);
        free (p);
        errno = saved_errno;
    }
}

static struct priority *priority_create (struct queue *queue,
                                   struct job *job,
                                   const flux_msg_t *request,
                                   int priority)
{
    struct priority *p;

    if (!(p = calloc (1, sizeof (*p))))
        return NULL;
    p->queue = queue;
    p->job = job;
    p->priority = priority;
    if (!(p->request = flux_msg_copy (request, false)))
        goto error;
    if (!(p->txn = flux_kvs_txn_create ()))
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
    queue_reorder (p->queue, p->job);
    if (flux_respond (h, p->request, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
done:
    priority_destroy (p);
    flux_future_destroy (f);
}

void priority_handle_request (flux_t *h, struct queue *queue,
                              const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    struct priority *p = NULL;
    flux_future_t *f;
    int priority;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "priority", &priority) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (priority < FLUX_JOB_PRIORITY_MIN || priority > FLUX_JOB_PRIORITY_MAX) {
        errno = EINVAL;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id)))
        goto error;
    /* Security: guests can only adjust jobs that they submitted.
     */
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job->userid) {
        errno = EPERM;
        goto error;
    }
    /* Security: guests can only reduce priority, or increase up to default.
     */
    if (!(rolemask & FLUX_ROLE_OWNER)
            && priority > MAXOF (FLUX_JOB_PRIORITY_DEFAULT, job->priority)) {
        errno = EPERM;
        goto error;
    }
    /* If job has requested resources/exec, don't allow adjustment.
     */
    if (job->flags != 0) {
        errno = EPERM;
        goto error;
    }
    /* Log KVS event and set KVS priority key asynchronously.
     * Upon successful completion, insert job in new queue position and
     * send response.
     */
    if (!(p = priority_create (queue, job, msg, priority)))
        goto error;
    if (active_eventlog_append (p->txn, job, "eventlog", "priority",
                                "userid=%lu priority=%d",
                                (unsigned long)userid, priority) < 0)
        goto error;
    if (active_pack (p->txn, job, "priority", "i", priority) < 0)
        goto error;
    if (!(f = flux_kvs_commit (h, NULL, 0, p->txn)))
        goto error;
    if (flux_future_then (f, -1., priority_continuation, p) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    priority_destroy (p);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
