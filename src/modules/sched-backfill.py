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

Implements the EASY backfill algorithm.  The highest-priority pending job
(the "head" job) receives a resource reservation: it is guaranteed to start
no later than the earliest time that enough running jobs will have finished
to satisfy its request (the "shadow time").  Lower-priority jobs may run
immediately (backfill) provided they meet both of the EASY constraints:

1. Fit within the currently free resources.
2. Have a known duration short enough that they will finish before the
   head job's shadow time, so the reservation is not violated.

Jobs submitted without a duration (``--time-limit=0``) cannot be backfilled
because their finish time is unknown.

The shadow time is computed by running the allocator against a temporary
copy of the resource pool and advancing through expiration events until the
head job fits.  This gives an accurate shadow time that respects topology
and property constraints — it tightens the backfill window compared to
count-based heuristics without over-constraining candidates.

Load with::

    flux module load sched-backfill [queue-depth=N|unlimited] [log-level=LEVEL]

``queue-depth`` bounds the number of pending alloc requests kept in the
scheduler queue (default 8).  Set to ``unlimited`` to expose the full queue.

``log-level`` sets the minimum syslog level for scheduler and pool log
messages (default ``err``; use ``debug`` for verbose output).

Reference:
    Ahuva W. Mu'alem and Dror G. Feitelson, "Utilization, Predictability,
    Workloads, and User Runtime Estimates in Scheduling the IBM SP2 with
    Backfilling," IEEE Transactions on Parallel and Distributed Systems,
    12(6):529-543, June 2001.
"""

import heapq
import time

from _flux._core import lib
from flux.job import JobID
from flux.resource import InfeasibleRequest, InsufficientResources
from flux.scheduler import Scheduler


class BackfillScheduler(Scheduler):
    """EASY backfill scheduler.

    Overrides:
      - :meth:`expiration` — update the pool's end-time tracking for the job
      - :meth:`cancel`     — clear annotations before removing from queue
      - :meth:`schedule`   — head-first with backfill for lower-priority jobs

    Helper methods (not base-class overrides):
      - :meth:`_try_alloc`        — attempt a real allocation, handling exceptions
      - :meth:`_shadow_time`      — compute head job's reservation time via simulation
      - :meth:`_annotate_pending` — send ``t_estimate`` annotation to the head job
    """

    def __init__(self, h, *args):
        super().__init__(h, *args)
        # Cache for _shadow_time(): keyed on (pool.generation, head.jobid).
        # pool.generation is bumped on every pool mutation (job start/complete,
        # node up/down), so the cache is automatically invalidated whenever
        # the shadow time could have changed.  Canceling a non-head pending
        # job does not mutate the pool and therefore does not invalidate it.
        self._shadow_cache_key = None
        self._shadow_cache_value = None

    # ------------------------------------------------------------------
    # Scheduler overrides
    # ------------------------------------------------------------------

    def expiration(self, msg, jobid, expiration):
        """Update the running job's end time in the resource pool.

        Called when a job's time limit is extended or reduced.  Keeping the
        pool's end-time tracking current is essential for accurate shadow-time
        computation: a stale end time would cause the simulation to free the
        job's resources at the wrong moment.
        """
        self.resources.update_expiration(jobid, expiration)
        self.handle.respond(msg, None)

    def cancel(self, jobid):
        """Remove a pending job from the queue, clearing its annotations first."""
        for job in self._queue:
            if job.jobid == jobid:
                job.request.annotate(
                    {"sched": {"resource_summary": None, "t_estimate": None}}
                )
                break
        super().cancel(jobid)

    # ------------------------------------------------------------------
    # Scheduling logic
    # ------------------------------------------------------------------

    def _try_alloc(self, job, backfill=None):
        """Attempt to allocate resources for a job.

        Args:
            job: A :class:`~flux.scheduler.PendingJob` to allocate.
            backfill: If not ``None``, the jobid of the head job whose
                reservation this job is backfilling around; the annotation
                records this relationship.

        Returns:
            True if the job was handled (alloc succeeded or request was
            permanently denied); False if resources are temporarily
            insufficient (:exc:`~flux.resource.InsufficientResources`).
        """
        rr = job.resource_request
        try:
            alloc = self.resources.alloc(job.jobid, rr)
        except InsufficientResources:
            return False  # not enough resources now — retry after next free
        except InfeasibleRequest as exc:
            job.request.deny(str(exc) or "unsatisfiable request")
            return True

        # Stamp the alloc R object with wall-clock start/expiration times
        # before handing it back to job-manager via RFC 27 success response.
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
        job.request.success(alloc, annotations)
        return True

    def _annotate_pending(self, shadow):
        """Post ``t_estimate`` annotation on the head-of-queue job.

        Only sends the annotation when the value has changed since the last
        send, to avoid redundant RPC overhead.

        Args:
            shadow: Shadow time in seconds since the Unix epoch, or ``None``
                if the shadow time is unknown.
        """
        if not self._queue:
            return
        head = sorted(self._queue)[0]
        if head._last_annotation != shadow:
            head._last_annotation = shadow
            head.request.annotate({"sched": {"t_estimate": shadow}})

    def _shadow_time(self, head):
        """Compute the EASY reservation time for the head job via direct simulation.

        Creates a temporary deep copy of the resource pool and advances it
        through expiration events until the head job's request fits.  Using
        the allocator directly (rather than count-based heuristics) gives an
        accurate shadow time that respects topology and property constraints,
        tightening the backfill window without over-constraining candidates.

        The result is cached by ``(pool.generation, head.jobid)``.
        ``pool.generation`` is bumped on every pool mutation (job start/complete,
        node up/down), so the cache is automatically invalidated when the shadow
        time may have changed.  Canceling a lower-priority pending job leaves the
        pool unchanged and does not invalidate the cache.

        Args:
            head: The highest-priority :class:`~flux.scheduler.PendingJob`
                for which the reservation is being computed.

        Returns:
            The earliest wall-clock time (seconds since the Unix epoch) at
            which the head job is guaranteed to fit, or ``None`` if the
            request is permanently infeasible or no future expirations remain.
        """
        key = (self.resources.generation, head.jobid)
        if self._shadow_cache_key == key:
            return self._shadow_cache_value

        sim = self.resources.copy()
        rr = head.resource_request
        t = 0.0
        shadow = None
        while True:
            try:
                sim.alloc(head.jobid, rr)
                shadow = max(t, time.time())  # map sim time to wall clock
                break
            except InfeasibleRequest:
                break  # permanently infeasible — shadow stays None
            except InsufficientResources:
                # Not enough resources now; advance to the next expiration.
                nxt = min(
                    (e for _, e in sim.job_end_times() if e > t),
                    default=None,
                )
                if nxt is None:
                    break  # no future expirations — shadow stays None
                t = nxt
                for jid, end_time in list(sim.job_end_times()):
                    if 0 < end_time <= t:
                        sim.free(jid)

        self._shadow_cache_key = key
        self._shadow_cache_value = shadow
        return shadow

    def schedule(self):
        """Schedule the head job; backfill lower-priority jobs around its reservation.

        If the head job allocates immediately, recurse to handle the new head
        (which may also be immediately schedulable).  If it is blocked, compute
        its shadow time and attempt to backfill each lower-priority job that:

        1. Has a known duration, and
        2. Will finish (``now + duration``) before the shadow time — the EASY
           constraint that guarantees the head job's reservation is not violated.
        """
        if not self._queue:
            return

        head = self._queue[0]

        # Only the head job carries a t_estimate annotation.  Clear any stale
        # annotation on non-head jobs (e.g. a job displaced by a higher-priority
        # arrival since the last schedule() call).
        for job in self._queue[1:]:
            if job._last_annotation is not None:
                job._last_annotation = None
                job.request.annotate({"sched": {"t_estimate": None}})

        if self._try_alloc(head):
            heapq.heappop(self._queue)
            yield from self.schedule()  # chain generator so reactor stays live during recursion
            return

        # Head is blocked — compute its shadow time (EASY reservation).
        shadow = self._shadow_time(head)
        now = time.time()

        kept = [head]
        for job in sorted(self._queue[1:]):  # priority order among non-head jobs
            duration = job.resource_request.duration
            # EASY constraint: backfill only if the job finishes before shadow.
            can_backfill = (
                shadow is not None and duration > 0.0 and now + duration <= shadow
            )
            if can_backfill and self._try_alloc(job, backfill=head.jobid):
                self.log.debug(f"backfill: {JobID(job.jobid).f58} (shadow={shadow:.0f})")
            else:
                kept.append(job)
            yield

        self._queue = kept
        heapq.heapify(self._queue)
        self._annotate_pending(shadow)


def mod_main(h, *args):
    BackfillScheduler(h, *args).run()
