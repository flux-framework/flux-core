/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

/* barrier.c - shell barrier
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <flux/core.h>
#include <flux/idset.h>

#include "src/common/libjob/idf58.h"
#include "src/common/libutil/errprintf.h"

#include "job-exec.h"
#include "barrier.h"

struct barrier {
    struct jobinfo *job;
    struct idset *pending_ranks;
    int enter_count;
    int exit_count;
    int total;
    struct flux_msglist *requests;
    flux_watcher_t *timer;
};

static void barrier_release (struct barrier *b, int errnum)
{
    const flux_msg_t *msg;
    struct jobinfo *job = b->job;

    if (!b->requests)
        return;
    while ((msg = flux_msglist_first (b->requests))) {
        int rc;
        if (errnum == 0)
            rc = shell_barrier_respond (job->h, msg, NULL);
        else
            rc = shell_barrier_respond_error (job->h, msg, errnum, NULL);
        if (rc < 0)
            flux_log_error (job->h, "shell-barrier: error responding to shell");
        flux_msglist_delete (b->requests);
    }
}

static void barrier_timer_cb (flux_reactor_t *r,
                              flux_watcher_t *w,
                              int revents,
                              void *arg)
{
    struct barrier *b = arg;
    struct jobinfo *job = b->job;
    char *ranks;

    if (!(ranks = idset_encode (b->pending_ranks, IDSET_FLAG_RANGE))) {
        flux_log_error (job->h,
                        "failed to encode barrier pending ranks for job %s",
                        idf58 (job->id));
        return;
    }

    /*  User-facing drain reason and exception message - don't change. */
    (void) jobinfo_drain_ranks (job,
                                ranks,
                                "job %s start timeout: %s",
                                idf58 (job->id),
                                "possible node hang");

    jobinfo_fatal_error (job,
                         0,
                         "%s waiting for %zu/%d nodes (rank%s %s)",
                         "start barrier timeout",
                         idset_count (b->pending_ranks),
                         b->total,
                         idset_count (b->pending_ranks) > 1 ?"s":"",
                         ranks);
    free (ranks);
}

int barrier_enter (struct barrier *b, const flux_msg_t *msg)
{
    struct jobinfo *job = b->job;
    int rank;

    if (flux_msg_unpack (msg, "{s:i}", "rank", &rank) < 0)
        return BARRIER_ERROR;
    /*  Reject a request whose rank is out of range or not pending
     *  to avoid corrupting barrier accounting.
     */
    if (rank < 0 || !idset_test (b->pending_ranks, rank)) {
        flux_error_t error;
        errprintf (&error, "rank %d is not pending in barrier", rank);
        if (shell_barrier_respond_error (job->h,
                                         msg,
                                         EINVAL,
                                         error.text) < 0)
            flux_log_error (job->h, "shell-barrier: error responding");
        return BARRIER_PENDING;
    }
    (void) idset_clear (b->pending_ranks, rank);
    b->enter_count++;

    /*
     *  Terminate barrier with error immediately when a shell enters after
     *   one or more shells have already exited. The case where a shell exits
     *   while a barrier is already in progress is handled in exit_cb().
     */
    if (b->exit_count > 0) {
        if (shell_barrier_respond_error (job->h, msg, EIO, NULL) < 0)
            flux_log_error (job->h, "shell-barrier: error responding");
        return BARRIER_PENDING;
    }

    if (flux_msglist_append (b->requests, msg) < 0)
        return BARRIER_ERROR;

    if (b->enter_count == b->total) {
        barrier_release (b, 0);
        flux_watcher_stop (b->timer);
        return BARRIER_COMPLETE;
    }
    /*  When the first shell enters the barrier, start a timer after
     *   which the job will be terminated if all shells have not reached
     *   the barrier.
     */
    if (b->enter_count == 1)
        flux_watcher_start (b->timer);

    return BARRIER_PENDING;
}

void barrier_notify_shell_exit (struct barrier *b)
{
    b->exit_count++;

    /*  Terminate any barrier in progress with error, releasing shells
     *   currently waiting so they exit immediately rather than being killed
     *   by the exec system.  Shells that enter the barrier later are
     *   rejected by barrier_enter() since exit_count is now nonzero.
     */
    barrier_release (b, EIO);
}

int barrier_drain_pending (struct barrier *b, const struct idset *ranks)
{
    struct idset *drain_ranks;
    char *drain_ids = NULL;
    int rc = -1;

    if (!(drain_ranks = idset_intersect (ranks, b->pending_ranks))
        || !(drain_ids = idset_encode (drain_ranks, IDSET_FLAG_RANGE)))
        goto fail;

    /*  User-facing drain reason - don't change. */
    if (idset_count (drain_ranks) > 0
        && jobinfo_drain_ranks (b->job,
                                drain_ids,
                                "%s terminated before first barrier",
                                idf58 (b->job->id)) < 0)
        goto fail;

    rc = 0;

fail:
    idset_destroy (drain_ranks);
    free (drain_ids);
    return rc;
}

void barrier_destroy (struct barrier *b)
{
    if (b) {
        int saved_errno = errno;
        /*  Respond with an error to any requests still parked in the
         *  barrier so those shells exit rather than blocking until killed.
         */
        barrier_release (b, EIO);
        flux_msglist_destroy (b->requests);
        idset_destroy (b->pending_ranks);
        flux_watcher_destroy (b->timer);
        free (b);
        errno = saved_errno;
    }
}

struct barrier *barrier_create (struct jobinfo *job,
                                const struct idset *ranks,
                                double timeout,
                                flux_error_t *errp)
{
    struct barrier *b = NULL;
    flux_reactor_t *r;

    if (!(r = flux_get_reactor (job->h))
        || !(b = calloc (1, sizeof (*b)))
        || !(b->requests = flux_msglist_create ())
        || !(b->pending_ranks = idset_copy (ranks))) {
        errprintf (errp, "%s", strerror (errno));
        goto error;
    }
    b->job = job;
    b->total = idset_count (ranks);

    if (timeout > 0.) {
        b->timer = flux_timer_watcher_create (r,
                                              timeout,
                                              0.,
                                              barrier_timer_cb,
                                              b);
        if (!b->timer) {
            errprintf (errp,
                       "%s: failed to create barrier timer",
                       idf58 (job->id));
            goto error;
        }
    }
    return b;
error:
    barrier_destroy (b);
    return NULL;
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
