/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* cancel - abort job
 *
 * Purpose:
 *   Handle job-manager.cancel RPC
 *
 * Input:
 * - job id
 * - flags
 *
 * Action:
 * - publish exception event (for e.g. scheduler to abort queued requests)
 * - update kvs event log
 * - response indicating success or failure
 * - removal from queue once job no longer has pending resource actions
 *
 * Caveats:
 * - Although the first error encountered during cancellation is propagated
 *   to the user, no attempt is made to unwind prior successful actions.
 * - Job is left in the active KVS area (needs to be moved to inactive).
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libjob/job.h"

#include "job.h"
#include "queue.h"
#include "util.h"
#include "cancel.h"

struct cancel {
    flux_t *h;
    flux_msg_t *request;
    struct job *job;
    flux_kvs_txn_t *txn;
    uint32_t userid;
    int errnum;
    char *errstr;

    struct queue *queue;
    int refcount;
};

static void cancel_respond (struct cancel *c)
{
    int rc;

    if (c->errnum != 0 && c->errstr != NULL)
        rc = flux_respond_error (c->h, c->request, c->errnum, "%s", c->errstr);
    else if (c->errnum != 0 && c->errstr == NULL)
        rc = flux_respond_error (c->h, c->request, c->errnum, NULL);
    else
        rc = flux_respond (c->h, c->request, 0, NULL);
    if (rc < 0)
        flux_log_error (c->h, "%s: flux_respond", __FUNCTION__);
}

/* If c->message is non-NULL, assume refcount reaches zero when all (parallel)
 * work to accomplish the cancel has been completed.  If any errors occurred,
 * c->errno will be non-zero.  Respond to the request and, advance the job
 * state to CLEANUP.
 */
static void cancel_decref (struct cancel *c)
{
    if (c && --c->refcount == 0) {
        int saved_errno = errno;
        c->job->flags &= ~JOB_EXCEPTION_PENDING;
        c->job->state = FLUX_JOB_CLEANUP;
        if (c->request) {
            cancel_respond (c);
            flux_msg_destroy (c->request);
        }
        flux_kvs_txn_destroy (c->txn);
        free (c->errstr);
        free (c);
        errno = saved_errno;
    }
}

static struct cancel *cancel_incref (struct cancel *c)
{
    if (c)
        c->refcount++;
    return c;
}

static struct cancel *cancel_create (flux_t *h,
                                     struct queue *queue,
                                     struct job *job,
                                     const flux_msg_t *request,
                                     uint32_t userid)
{
    struct cancel *c;

    if (!(c = calloc (1, sizeof (*c))))
        return NULL;
    c->queue = queue;
    c->job = job;
    c->userid = userid;
    c->h = h;
    c->refcount = 1;
    if (!(c->txn = flux_kvs_txn_create ()))
        goto error;
    if (!(c->request = flux_msg_copy (request, false)))
        goto error;
    return c;
error:
    cancel_decref (c);
    return NULL;
}

static void cancel_set_error (struct cancel *c, int errnum, const char *errstr)
{
    if (c && c->errnum == 0) {
        int saved_errno = errno;
        c->errnum = errno;
        free (c->errstr);
        c->errstr = NULL;
        if (errstr) {
            if (!(c->errstr = strdup (errstr)))
                flux_log_error (c->h, "%s: strdup", __FUNCTION__);
        }
        errno = saved_errno;
    }
}

static void publish_exception_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct cancel *c = arg;

    if (flux_future_get (f, NULL) < 0) {
        cancel_set_error (c, errno, "error publishing job-exception event");
        flux_log_error (h, "publish job-exception id=%llu",
                        (unsigned long long)c->job->id);
    }
    flux_future_destroy (f);
    cancel_decref (c);
}

/* Publish a 'job-exception' event message.
 */
static void publish_exception (struct cancel *c)
{
    flux_future_t *f;

    if (!(f = flux_event_publish_pack (c->h, "job-exception",
                                       FLUX_MSGFLAG_PRIVATE,
                                       "{s:I s:s s:i}",
                                       "id", c->job->id,
                                       "type", "cancel",
                                       "severity", 0)))
        goto error;
    if (flux_future_then (f, -1., publish_exception_continuation, c) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    cancel_incref (c);
    return;
error:
    cancel_set_error (c, errno, "error publishing job-exception event");
    flux_log_error (c->h, "publish job-exception id=%llu",
                    (unsigned long long)c->job->id);
}

static void update_kvs_eventlog_continuation (flux_future_t *f, void *arg)
{
    struct cancel *c = arg;
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0) {
        cancel_set_error (c, errno, "error updating KVS event log");
        flux_log_error (h, "eventlog_update id=%llu",
                        (unsigned long long)c->job->id);
    }
    flux_future_destroy (f);
    cancel_decref (c);
}

/* Log exception to the job's eventlog.
 */
static void update_kvs_eventlog (struct cancel *c)
{
    flux_future_t *f;

    if (util_eventlog_append (c->txn, c->job, "exception",
                              "type=cancel severity=0 userid=%lu",
                              (unsigned long)c->userid) < 0)
        goto error;
    if (!(f = flux_kvs_commit (c->h, NULL, 0, c->txn)))
        goto error;
    if (flux_future_then (f, -1, update_kvs_eventlog_continuation, c) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    cancel_incref (c);
    return;
error:
    cancel_set_error (c, errno, "error updating KVS event log");
    flux_log_error (c->h, "eventlog_update id=%llu",
                    (unsigned long long)c->job->id);
}

void cancel_handle_request (flux_t *h, struct queue *queue,
                            const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    int flags;
    struct cancel *c;
    const char *errstr = NULL;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:I s:i}",
                                        "id", &id,
                                        "flags", &flags) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (flags != 0) {
        errno = EPROTO;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id))) {
        errstr = "unknown job id";
        goto error;
    }
    /* Security: guests can only cancel jobs that they submitted.
     */
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job->userid) {
        errstr = "guests can only cancel their own jobs";
        errno = EPERM;
        goto error;
    }
    /* Perform some tasks asynchronously.
     * When the last one completes, 'c' is destroyed and
     * the user receives a response to the job-manager.cancel request.
     */
    if (!(c = cancel_create (h, queue, job, msg, userid)))
        goto error;
    job->flags |= JOB_EXCEPTION_PENDING;
    update_kvs_eventlog (c);
    publish_exception (c);
    cancel_decref (c);
    return;
error:
    if (errstr)
        rc = flux_respond_error (h, msg, errno, "%s", errstr);
    else
        rc = flux_respond_error (h, msg, errno, NULL);
    if (rc < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
