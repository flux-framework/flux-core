/************************************************************\
 * Copyright 2018 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* purge - remove job
 *
 * Purpose:
 *   Support "flux job purge" command to remove a job from the queue and KVS.
 *   This allows backing out of a job that was submitted in error, or is no
 *   longer needed, without contributing noise to the job historical data.
 *
 *   Purge is also helpful in writing tests of job-manager queue management.
 *
 * Input:
 * - job id
 * - flags (set to 0)
 *
 * Output:
 * - n/a
 *
 * Caveats:
 * - No flag to force removal if resources already requested/allocated.
 */

#if HAVE_CONFIG_H
#include "config.h"
#endif
#include <flux/core.h>

#include "src/common/libjob/job.h"

#include "job.h"
#include "queue.h"
#include "active.h"
#include "purge.h"

struct purge {
    flux_msg_t *request;
    struct job *job;
    flux_kvs_txn_t *txn;
    int flags;

    struct queue *queue;
};

static void purge_destroy (struct purge *r)
{
    if (r) {
        int saved_errno = errno;
        flux_msg_destroy (r->request);
        flux_kvs_txn_destroy (r->txn);
        free (r);
        errno = saved_errno;
    }
}

static struct purge *purge_create (struct queue *queue,
                                   struct job *job,
                                   const flux_msg_t *request,
                                   int flags)
{
    struct purge *r;

    if (!(r = calloc (1, sizeof (*r))))
        return NULL;
    r->queue = queue;
    r->job = job;
    r->flags = flags;
    if (!(r->request = flux_msg_copy (request, false)))
        goto error;
    if (!(r->txn = flux_kvs_txn_create ()))
        goto error;
    return r;
error:
    purge_destroy (r);
    return NULL;
}

/* KVS unlink completed.  Remove job from queue and respond.
 */
static void purge_continuation (flux_future_t *f, void *arg)
{
    flux_t *h = flux_future_get_flux (f);
    struct purge *r = arg;

    if (flux_rpc_get (f, NULL) < 0) {
        if (flux_respond_error (h, r->request, errno, NULL) < 0)
            flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
        goto done;
    }
    queue_delete (r->queue, r->job);
    if (flux_respond (h, r->request, 0, NULL) < 0)
        flux_log_error (h, "%s: flux_respond", __FUNCTION__);
done:
    purge_destroy (r);
    flux_future_destroy (f);
}

void purge_handle_request (flux_t *h, struct queue *queue,
                           const flux_msg_t *msg)
{
    uint32_t userid;
    uint32_t rolemask;
    flux_jobid_t id;
    struct job *job;
    struct purge *r = NULL;
    flux_future_t *f;
    int flags;

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
    if (!(job = queue_lookup_by_id (queue, id)))
        goto error;
    /* Security: guests can only remove jobs that they submitted.
     */
    if (!(rolemask & FLUX_ROLE_OWNER) && userid != job->userid) {
        errno = EPERM;
        goto error;
    }
    /* If job has requested resources/exec, don't allow purge.
     */
    if (job->flags != 0) {
        errno = EPERM;
        goto error;
    }
    /* Perfrom KVS unlink asynchronously.
     * Upon successful completion, remove job from queue and send response.
     */
    if (!(r = purge_create (queue, job, msg, flags)))
        goto error;
    if (active_unlink (r->txn, job) < 0)
        goto error;
    if (!(f = flux_kvs_commit (h, NULL, 0, r->txn)))
        goto error;
    if (flux_future_then (f, -1., purge_continuation, r) < 0) {
        flux_future_destroy (f);
        goto error;
    }
    return;
error:
    if (flux_respond_error (h, msg, errno, NULL) < 0)
        flux_log_error (h, "%s: flux_respond_error", __FUNCTION__);
    purge_destroy (r);
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
