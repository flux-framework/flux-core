/************************************************************\
 * Copyright 2026 Lawrence Livermore National Security, LLC
 * (c.f. AUTHORS, NOTICE.LLNS, COPYING)
 *
 * This file is part of the Flux resource manager framework.
 * For details, see https://github.com/flux-framework.
 *
 * SPDX-License-Identifier: LGPL-3.0
\************************************************************/

#ifndef HAVE_JOB_EXEC_BARRIER_H
#define HAVE_JOB_EXEC_BARRIER_H 1

#include <flux/core.h>
#include <flux/idset.h>

struct jobinfo;

/*  A shell barrier: a one-shot, all-ranks rendezvous.
 *
 *  Job shells enter a barrier at points during startup (after
 *  initialization, and again after tasks are started).  The barrier
 *  completes when all shells have entered, at which point they are released
 *  together.  A timer (optional) terminates the job if not all shells enter
 *  within the configured timeout, since that likely indicates a hung node.
 *
 *  This is a self-contained unit: it is a single rendezvous over a fixed
 *  rank set and knows nothing of the sequence of barriers a job performs.
 *  The caller creates one per barrier (destroying the previous one on
 *  completion), so the "first barrier is timed" policy lives in the caller,
 *  not here.  It is driven entirely by plain values so it does not depend on
 *  a particular exec backend.
 */

struct barrier;

enum barrier_status {
    BARRIER_ERROR    = -1,  /* internal failure; errno set */
    BARRIER_PENDING  =  0,  /* not all entered, or request rejected */
    BARRIER_COMPLETE =  1,  /* all ranks entered and were released */
};

/*  Create a barrier over the shell ranks in `ranks`.  `timeout` is the
 *  barrier timeout in seconds; if <= 0 no timer is armed.
 */
struct barrier *barrier_create (struct jobinfo *job,
                                const struct idset *ranks,
                                double timeout,
                                flux_error_t *errp);

/*  Destroy the barrier, responding EIO to any still-parked requests so that
 *  waiting shells exit rather than blocking until killed.
 */
void barrier_destroy (struct barrier *b);

/*  Handle a shell's barrier-enter request.  Rejects a request whose rank is
 *  out of range or not pending.  Returns BARRIER_COMPLETE when this request
 *  is the last to enter (all parked requests have been released),
 *  BARRIER_PENDING when more ranks are still expected (including the
 *  rejected-request case, which is reported to the shell), or BARRIER_ERROR
 *  with errno set on internal failure.
 */
int barrier_enter (struct barrier *b, const flux_msg_t *msg);

/*  Notify the barrier that one or more shells have exited.  Terminates the
 *  in-progress barrier with EIO (releasing waiting shells so they exit) and
 *  causes subsequent barrier-enter requests to be rejected.
 */
void barrier_notify_shell_exit (struct barrier *b);

/*  Drain the subset of `ranks` that are still pending in the barrier.
 *  Returns 0 on success, -1 with errno set on failure.
 */
int barrier_drain_pending (struct barrier *b, const struct idset *ranks);

#endif /* !HAVE_JOB_EXEC_BARRIER_H */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */
