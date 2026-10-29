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

Sometimes no shadow time can be computed.  There are two reasons, and
they are handled differently:

- The job does not fit on an idle cluster, because the resources it
  needs are drained, down, or gone.  No expiration will free them, so
  the job is set aside and the next job becomes the head.  It keeps
  its place in the queue and runs as soon as the resources return.
- The running jobs have no known end time.  The job would fit once
  they end, so it stays the head and backfill stops behind it.  This
  keeps a stream of short jobs from starving it.

Setting a job aside relaxes the EASY guarantee, as other schedulers
also do.  A reservation is a point in time, and a job that can never
run has none to protect.  Blocking the queue would not start it any
sooner, and would idle the cluster until its resources return.

A job set aside for a long time can still be delayed by jobs that
backfilled meanwhile.  Nothing bounds that here.  The usual fixes are
priority aging, or a drain end time used as the shadow time, which
would make this the second case above.  Flux drains have no end time.

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
      - :meth:`_annotate_pending` — maintain ``t_estimate`` across the queue
    """

    def __init__(self, h, *args):
        super().__init__(h, *args)
        # Cache for _shadow_time(), keyed by jobid and discarded whenever
        # pool.generation changes.  pool.generation is bumped on every pool
        # mutation (job start/complete, node up/down), so a shadow time is
        # dropped whenever it could have changed.  Canceling a non-head
        # pending job does not mutate the pool and does not invalidate it.
        #
        # N.B. a dict rather than a single slot: one pass may consult
        # several head jobs, which a single slot would thrash.
        self._shadow_cache = {}
        self._shadow_cache_gen = None

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

    def _annotate_pending(self, head, shadow):
        """Maintain the ``t_estimate`` annotation across the pending queue.

        Exactly one pending job carries a ``t_estimate``: the one holding
        the reservation.  It is set here and cleared everywhere else, so
        that a job which was the head on an earlier pass does not keep a
        stale estimate.  An annotation is sent only when its value has
        changed, to avoid redundant RPC overhead.

        N.B. the job holding the reservation is not always the
        highest-priority pending one.  A job that cannot run until
        resources return is set aside and sorts ahead of it, so the
        caller passes the job it computed the shadow time for.

        Args:
            head: The :class:`~flux.scheduler.PendingJob` holding the
                reservation, or None if no job does.
            shadow: Shadow time in seconds since the Unix epoch, or ``None``
                if the shadow time is unknown.
        """
        for job in self._queue:
            value = shadow if job is head else None
            if job._last_annotation != value:
                job._last_annotation = value
                job.request.annotate({"sched": {"t_estimate": value}})

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
            A (shadow, never) tuple.  'shadow' is the earliest wall-clock
            time at which the head job is guaranteed to fit, or None if
            no such time could be determined.  'never' is True when the
            job does not fit even on a completely idle cluster, so no
            expiration can ever make room for it; see the module
            docstring for why the two None cases differ.
        """
        generation = self.resources.generation
        if generation != self._shadow_cache_gen:
            self._shadow_cache.clear()
            self._shadow_cache_gen = generation
        if head.jobid in self._shadow_cache:
            return self._shadow_cache[head.jobid]

        sim = self.resources.copy()
        rr = head.resource_request
        t = 0.0
        shadow = None
        never = False
        while True:
            try:
                sim.alloc(head.jobid, rr)
                shadow = max(t, time.time())  # map sim time to wall clock
                break
            except InfeasibleRequest:
                never = True  # cannot be satisfied by this cluster at all
                break
            except InsufficientResources:
                # Not enough resources now; advance to the next expiration.
                nxt = min(
                    (e for _, e in sim.job_end_times() if e > t),
                    default=None,
                )
                if nxt is None:
                    # No expiration left to wait for.  Everything with a
                    # known end time has already been freed above, so
                    # freeing the rest leaves an idle cluster: if the job
                    # still does not fit, nothing will ever make room for
                    # it.  Otherwise it is only the unknown end times
                    # that prevent a shadow time being computed.
                    for jid, _ in sim.job_end_times():
                        sim.free(jid)
                    try:
                        sim.alloc(head.jobid, rr)
                    except (InfeasibleRequest, InsufficientResources):
                        never = True
                    break
                t = nxt
                for jid, end_time in sim.job_end_times():
                    if 0 < end_time <= t:
                        sim.free(jid)

        self._shadow_cache[head.jobid] = (shadow, never)
        return (shadow, never)

    def schedule(self):
        """Schedule what fits; backfill the rest around the head's reservation.

        Jobs are taken in priority order and allocated while they fit.  The
        first job that does not becomes the head and receives a reservation,
        and each lower-priority job may then backfill if it:

        1. Has a known duration, and
        2. Will finish (``now + duration``) before the shadow time — the EASY
           constraint that guarantees the head job's reservation is not violated.
        """
        if not self._queue:
            return

        # Walk the queue in priority order, allocating what fits, until a
        # job is found that must hold a reservation.
        #
        # A job that cannot be allocated and has no shadow time because it
        # does not fit even on an idle cluster is waiting on resources that
        # no expiration will free.  It has no reservation for backfill to
        # respect, so it is set aside and the scan continues.  It keeps its
        # place in the queue and is reconsidered on every pass, so it runs
        # as soon as those resources return.
        #
        # Any other job becomes the effective head, and backfill proceeds
        # around its shadow time as usual.  That includes a job whose
        # shadow time is unknown because the running jobs have no known
        # end time: backfill stops behind it, so that a stream of short
        # jobs cannot starve it.
        # N.B. an allocated job is popped from self._queue before the
        # yield that follows it.  The yield returns to the reactor, where
        # an alloc or free callback may push or remove queue entries, so
        # the queue must be correct at every yield rather than written
        # back from a snapshot at the end of the pass.
        setaside = []
        head = None
        shadow = None
        while self._queue:
            job = heapq.heappop(self._queue)
            if self._try_alloc(job):
                yield
                continue
            shadow, never = self._shadow_time(job)
            if not never:
                head = job
                break
            self.log.debug(f"{JobID(job.jobid).f58}: no resources will free; set aside")
            setaside.append(job)
        # Whatever is left is the backfill candidate set.  As on the
        # pre-existing path, it is snapshotted here and written back at
        # the end of the pass.
        rest = sorted(self._queue)

        if head is None:
            # Every job was allocated or set aside; none holds a reservation.
            self._queue = setaside
            heapq.heapify(self._queue)
            self._annotate_pending(None, None)
            return

        now = time.time()

        kept = setaside + [head]
        for job in rest:
            duration = job.resource_request.duration
            # EASY constraint: backfill only if the job finishes before shadow.
            can_backfill = (
                shadow is not None and duration > 0.0 and now + duration <= shadow
            )
            if can_backfill and self._try_alloc(job, backfill=head.jobid):
                self.log.debug(
                    f"backfill: {JobID(job.jobid).f58} (shadow={shadow:.0f})"
                )
            else:
                kept.append(job)
            yield

        self._queue = kept
        heapq.heapify(self._queue)
        self._annotate_pending(head, shadow)


def mod_main(h, *args):
    BackfillScheduler(h, *args).run()
