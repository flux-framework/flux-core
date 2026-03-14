###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""EASY backfill scheduler broker module.

Implements the EASY backfill algorithm using the :class:`flux.scheduler.Scheduler`
base class.  The highest-priority pending job receives a resource reservation:
it is guaranteed to start no later than the earliest time that enough running
jobs will have finished to satisfy its request.  Lower-priority jobs may run
immediately (backfill) provided they:

1. Fit within the currently free resources, and
2. Have a known duration short enough that they will finish before the
   head job's estimated reservation time.

Jobs submitted without a duration (``--time-limit=0``) cannot be backfilled
because their finish time is unknown.

The shadow time is computed by running the allocator directly against a
temporary copy of the resource pool, advancing through expiration events
until the head job's request can be satisfied.  This gives an accurate
shadow time that respects topology constraints.  Returns ``None`` (no
backfill) if the pool does not support deep copy.

Load with::

    flux module load sched-backfill [queue-depth=N|unlimited]
    flux module load sched-backfill alloc-mode=worst-fit|best-fit|first-fit  # Rv1RlistPool only
    flux module load sched-backfill resource-class=Rv1Pool|Rv1RlistPool

Reference:
    Ahuva W. Mu'alem and Dror G. Feitelson, "Utilization, Predictability,
    Workloads, and User Runtime Estimates in Scheduling the IBM SP2 with
    Backfilling," IEEE Transactions on Parallel and Distributed Systems,
    12(6):529-543, June 2001.
"""

import errno
import heapq
import syslog
import time

from _flux._core import lib
from flux.job import JobID
from flux.scheduler import Scheduler


class BackfillScheduler(Scheduler):
    """EASY backfill scheduler using the Scheduler base class.

    Module arguments::

        flux module load sched-backfill [queue-depth=N|unlimited]
        flux module load sched-backfill [alloc-mode=worst-fit|best-fit|first-fit]  # Rv1RlistPool only
        flux module load sched-backfill [resource-class=Rv1Pool|Rv1RlistPool]
    """

    def __init__(self, h, *args):
        super().__init__(h, *args)
        # Cache for _shadow_time(): keyed on (pool.generation, head.jobid).
        # Invalidated whenever the resource pool changes (running job completes
        # or starts, node up/down) or the head job changes identity.  Canceling
        # a non-head pending job does not affect the shadow time, so the key
        # remains valid in that case.
        self._shadow_cache_key = None
        self._shadow_cache_value = None

    # ------------------------------------------------------------------
    # Scheduler overrides
    # ------------------------------------------------------------------

    def hello(self, jobid, priority, userid, t_submit, R):
        """Mark a running job's resources as allocated."""
        super().hello(jobid, priority, userid, t_submit, R)
        self.handle.log(
            syslog.LOG_DEBUG, f"hello: {JobID(jobid).f58} alloc={R.dumps()}"
        )

    def expiration(self, msg, jobid, expiration):
        """Update the running job's end time in the pool."""
        self.resources.update_expiration(jobid, expiration)
        self.handle.respond(msg, None)

    # ------------------------------------------------------------------
    # Scheduler override and internal scheduling logic
    # ------------------------------------------------------------------

    def cancel(self, jobid):
        """Remove a pending job from the queue, clearing its annotations first."""
        for job in self._queue:
            if job.jobid == jobid:
                job.request.annotate(
                    {
                        "sched": {
                            "resource_summary": None,
                            "t_estimate": None,
                        }
                    }
                )
                break
        super().cancel(jobid)

    def _try_alloc(self, job, backfill=None):
        """Attempt to allocate resources for *job*.

        Returns True if the job was handled (success or permanent deny),
        False if resources are temporarily insufficient (ENOSPC).
        """
        rr = job.resource_request
        try:
            alloc = self.resources.alloc(job.jobid, rr, mode=self._alloc_mode)
        except OSError as exc:
            if exc.errno == errno.ENOSPC:
                return False  # not enough resources now — retry later
            note = exc.strerror if exc.strerror else "unsatisfiable request"
            job.request.deny(note)
            return True

        # Set start/expiration times on the alloc for the RFC 27 R response.
        # The pool has already recorded the end time internally; here we stamp
        # the alloc object itself for serialization via to_dict().
        reactor = lib.flux_get_reactor(self.handle.handle)
        now = lib.flux_reactor_now(reactor)
        alloc.set_starttime(now)
        if rr.duration > 0.0:
            alloc.set_expiration(now + rr.duration)
        elif self.resources.expiration > 0.0:
            alloc.set_expiration(self.resources.expiration)

        summary = alloc.dumps()
        annotations = {"sched": {"resource_summary": summary}}
        if backfill is not None:
            annotations["sched"]["backfill"] = JobID(backfill).f58
        # RFC 27 sched annotation key: resource_summary (free-form string).
        # t_estimate is cleared automatically by AllocRequest.success().
        job.request.success(alloc.to_dict(), annotations)
        return True

    def _annotate_pending(self, shadow):
        """Post t_estimate on the head-of-queue job when a shadow time is known.

        shadow is seconds since the Unix epoch (float), or None if unknown.
        Only sends the annotation when the value has changed since the last
        send, to avoid redundant RPC overhead.
        """
        if not self._queue:
            return
        head = sorted(self._queue)[0]
        if head._last_annotation != shadow:
            head._last_annotation = shadow
            # RFC 27 sched annotation key: t_estimate (wall clock seconds
            # since Unix epoch, or null to clear).
            head.request.annotate({"sched": {"t_estimate": shadow}})

    def _shadow_time(self, head):
        """Compute the EASY reservation time for *head* via direct simulation.

        Creates a temporary deep copy of the resource pool and advances it
        through expiration events until the head job's request can be
        satisfied, returning the earliest such time.

        Using the allocator directly (rather than count-based heuristics)
        gives a more accurate shadow time, which tightens the backfill window
        and avoids either over- or under-constraining candidate backfill jobs.

        The resulting shadow time (a float, seconds since epoch) is cached by
        ``(pool.generation, head.jobid)`` so that repeated calls within the
        same scheduling pass return immediately without redoing the deepcopy or
        simulation.  The cache is invalidated when any pool mutation occurs
        (job start/complete, node up/down) — these bump ``pool.generation`` —
        or when the head job changes identity.  Canceling a lower-priority
        pending job does not affect the shadow time and does not invalidate the
        cache.
        """
        key = (self.resources.generation, head.jobid)
        if self._shadow_cache_key == key:
            return self._shadow_cache_value

        sim = self.resources.deepcopy()

        rr = head.resource_request
        t = 0.0
        shadow = None
        while True:
            try:
                sim.alloc(head.jobid, rr, mode=self._alloc_mode)
                shadow = max(t, time.time())
                break
            except OSError as exc:
                if exc.errno != errno.ENOSPC:
                    break  # permanently infeasible — shadow stays None
                nxt = min(
                    (e for e, _ in sim._job_state.values() if e > t),
                    default=None,
                )
                if nxt is None:
                    break  # no future expirations — shadow stays None
                t = nxt
                for jid, (end_time, _) in list(sim._job_state.items()):
                    if 0 < end_time <= t:
                        sim.free(jid)

        self._shadow_cache_key = key
        self._shadow_cache_value = shadow
        return shadow

    def schedule(self):
        """Schedule the head job; backfill lower-priority jobs around its reservation."""
        if not self._queue:
            return

        # Always try the head-of-queue job first
        head = self._queue[0]

        # Only the head job receives a t_estimate annotation.  Clear any stale
        # annotation on non-head jobs (e.g. a job that was head on a prior
        # schedule() call and has since been displaced by a higher-priority arrival).
        for job in self._queue[1:]:
            if job._last_annotation is not None:
                job._last_annotation = None
                job.request.annotate({"sched": {"t_estimate": None}})

        if self._try_alloc(head):
            heapq.heappop(self._queue)
            self.schedule()  # recurse to handle the new head
            return

        # Head is blocked — compute its shadow time (EASY reservation estimate)
        shadow = self._shadow_time(head)
        now = time.time()

        # Attempt to backfill lower-priority jobs that:
        #   1. Have a known duration, and
        #   2. Will finish before the head job's estimated reservation time
        kept = [head]
        for job in sorted(self._queue[1:]):  # priority order among non-head jobs
            duration = job.resource_request.duration
            can_backfill = (
                shadow is not None and duration > 0.0 and now + duration <= shadow
            )
            if can_backfill and self._try_alloc(job, backfill=head.jobid):
                self.handle.log(
                    syslog.LOG_DEBUG,
                    f"backfill: {JobID(job.jobid).f58} (shadow={shadow:.0f})",
                )
            else:
                kept.append(job)

        self._queue = kept
        heapq.heapify(self._queue)
        self._annotate_pending(shadow)


def mod_main(h, *args):
    BackfillScheduler(h, *args).run()
