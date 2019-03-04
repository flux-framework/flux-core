/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* raise - raise an exception on a job
 *
 * Purpose:
 *   Handle job-manager.raise RPC
 *
 * Input:
 * - job id, severity, type, note (optional)
 *
 * Action:
 * - publish exception event (for e.g. scheduler to abort queued requests)
 * - update kvs event log
 * - response indicating success or failure
 * - transition state to CLEANUP for severity=0
 *
 * Caveats:
 * - Although the first error encountered during parallel work is propagated
 *   to the user, no attempt is made to unwind prior successful actions.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <ctype.h>
#include <flux/core.h>

#include "src/common/libjob/job.h"

#include "job.h"
#include "queue.h"
#include "util.h"
#include "raise.h"

struct raise_ctx {
    flux_t *h;
    flux_msg_t *request;
    struct job *job;
    flux_kvs_txn_t *txn;
    uint32_t userid;
    int severity;
    char *type;
    char *note;
    int errnum;
    char *errstr;

    struct queue *queue;
    struct alloc_ctx *alloc_ctx;
    int refcount;
};

void raise_respond (struct raise_ctx *c)
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
 * work to raise the exception is complete.  If any errors occurred,
 * c->errno will be non-zero.  Respond to the request and, advance the job
 * state to CLEANUP (if severity=0)
 */
void raise_ctx_decref (struct raise_ctx *c)
{
    if (c && --c->refcount == 0) {
        int saved_errno = errno;
        if (c->request) {
            raise_respond (c);
            flux_msg_destroy (c->request);
        }
        if (c->severity == 0) {
            c->job->state = FLUX_JOB_CLEANUP;
            if (alloc_do_request (c->alloc_ctx, c->job) < 0) {
                flux_log_error (c->h,
                    "%s: error notifying scheduler of job %llu cleanup",
                    __FUNCTION__, (unsigned long long)c->job->id);
            }
        }
        flux_kvs_txn_destroy (c->txn);
        free (c->note);
        free (c->type);
        free (c->errstr);
        free (c);
        errno = saved_errno;
    }
}

struct raise_ctx *raise_ctx_incref (struct raise_ctx *c)
{
    if (c)
        c->refcount++;
    return c;
}

struct raise_ctx *raise_ctx_create (flux_t *h,
                                    struct queue *queue,
                                    struct alloc_ctx *alloc_ctx,
                                    struct job *job,
                                    const flux_msg_t *request,
                                    uint32_t userid,
                                    int severity,
                                    const char *type,
                                    const char *note)
{
    struct raise_ctx *c;

    if (!(c = calloc (1, sizeof (*c))))
        return NULL;
    c->queue = queue;
    c->alloc_ctx = alloc_ctx;
    c->job = job;
    c->userid = userid;
    c->h = h;
    c->refcount = 1;
    c->severity = severity;
    if (!(c->type = strdup (type)))
        goto error;
    if (note && !(c->note = strdup (note)))
        goto error;
    if (!(c->txn = flux_kvs_txn_create ()))
        goto error;
    if (!(c->request = flux_msg_copy (request, false)))
        goto error;
    return c;
error:
    raise_ctx_decref (c);
    return NULL;
}

void raise_set_error (struct raise_ctx *c, int errnum, const char *errstr)
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

void raise_publish_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct raise_ctx *c = arg;

    if (flux_future_get (f, NULL) < 0) {
        raise_set_error (c, errno, "error publishing job-exception event");
        flux_log_error (h, "publish job-exception id=%llu",
                        (unsigned long long)c->job->id);
    }
    flux_future_destroy (f);
    raise_ctx_decref (c);
}

/* Publish a 'job-exception' event message.
 */
void raise_publish (struct raise_ctx *c)
{
    flux_future_t *f;

    if (!(f = flux_event_publish_pack (c->h, "job-exception",
                                       FLUX_MSGFLAG_PRIVATE,
                                       "{s:I s:s s:i}",
                                       "id", c->job->id,
                                       "type", c->type,
                                       "severity", c->severity)))
        goto error;
    if (flux_future_then (f, -1., raise_publish_continuation, c) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    raise_ctx_incref (c);
    return;
error:
    raise_set_error (c, errno, "error publishing job-exception event");
    flux_log_error (c->h, "publish job-exception id=%llu",
                    (unsigned long long)c->job->id);
}

void raise_eventlog_continuation (flux_future_t *f, void *arg)
{
    struct raise_ctx *c = arg;
    flux_t *h = flux_future_get_flux (f);

    if (flux_future_get (f, NULL) < 0) {
        raise_set_error (c, errno, "error updating KVS event log");
        flux_log_error (h, "eventlog_update id=%llu",
                        (unsigned long long)c->job->id);
    }
    flux_future_destroy (f);
    raise_ctx_decref (c);
}

void raise_eventlog (struct raise_ctx *c)
{
    flux_future_t *f;

    if (util_eventlog_append (c->txn, c->job->id, "exception",
                              "type=%s severity=%d userid=%lu%s%s",
                              c->type,
                              c->severity,
                              (unsigned long)c->userid,
                              c->note ? " " : "",
                              c->note ? c->note : "") < 0)
        goto error;
    if (!(f = flux_kvs_commit (c->h, NULL, 0, c->txn)))
        goto error;
    if (flux_future_then (f, -1, raise_eventlog_continuation, c) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    raise_ctx_incref (c);
    return;
error:
    raise_set_error (c, errno, "error updating KVS event log");
    flux_log_error (c->h, "eventlog_update id=%llu",
                    (unsigned long long)c->job->id);
}

int raise_check_type (const char *s)
{
    const char *cp;

    if (strlen (s)  == 0)
        return -1;
    for (cp = s; *cp != '\0'; cp++)
        if (isspace (*cp) || *cp == '=')
            return -1;
    return 0;
}

int raise_check_severity (int severity)
{
    if (severity < 0 || severity > 7)
        return -1;
    return 0;
}

int raise_allow (uint32_t rolemask, uint32_t userid, uint32_t job_userid)
{
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job_userid)
        return -1;
    return 0;
}

void raise_handle_request (flux_t *h, struct queue *queue,
                           struct alloc_ctx *alloc_ctx,
                           const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    int severity;
    const char *type;
    const char *note = NULL;
    struct raise_ctx *c;
    const char *errstr = NULL;
    int rc;

    if (flux_request_unpack (msg, NULL, "{s:I s:i s:s s?:s}",
                                        "id", &id,
                                        "severity", &severity,
                                        "type", &type,
                                        "note", &note) < 0
                    || flux_msg_get_userid (msg, &userid) < 0
                    || flux_msg_get_rolemask (msg, &rolemask) < 0)
        goto error;
    if (raise_check_severity (severity)) {
        errstr = "invalid exception severity";
        errno = EPROTO;
        goto error;
    }
    if (raise_check_type (type) < 0) {
        errstr = "invalid exception type";
        errno = EPROTO;
        goto error;
    }
    if (!(job = queue_lookup_by_id (queue, id))) {
        errstr = "unknown job id";
        goto error;
    }
    if (raise_allow (rolemask, userid, job->userid) < 0) {
        errstr = "guests can only raise exceptions on their own jobs";
        errno = EPERM;
        goto error;
    }
    if (note && strchr (note, '=')) {
        errstr = "exception note may not contain key=value attributes";
        errno = EPROTO;
        goto error;
    }
    /* Perform some tasks asynchronously.
     * When the last one completes, 'c' is destroyed and
     * the user receives a response to the job-manager.raise request.
     */
    if (!(c = raise_ctx_create (h, queue, alloc_ctx, job, msg, userid,
                                severity, type, note)))
        goto error;
    raise_eventlog (c);
    raise_publish (c);
    raise_ctx_decref (c);
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
