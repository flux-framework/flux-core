###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Simple priority-FIFO scheduler broker module.

Jobs are ordered by descending priority then ascending jobid (FIFO within
a priority level), matching the behaviour of sched-simple.  The head of
the queue is attempted first; if it cannot be allocated the pass stops,
preserving strict FIFO ordering.

An optional ``forecast()`` implementation annotates each queued job with
a ``t_estimate`` by simulating allocations forward in time.

Load with::

    flux module load sched-fifo [queue-depth=N|unlimited] [log-level=LEVEL]

``queue-depth`` bounds the number of pending alloc requests kept in the
scheduler queue (default 8, matching sched-simple).  Set to ``unlimited``
if the full pending queue must be visible for policy or annotation purposes.

``log-level`` sets the minimum syslog level for scheduler and pool log
messages (default ``err``; use ``debug`` for verbose output).
"""

import heapq
import time as _time

from _flux._core import lib
from flux.resource import InfeasibleRequest, InsufficientResources
from flux.scheduler import Scheduler


class FIFOScheduler(Scheduler):
    """Priority-FIFO scheduler.

    Overrides:
      - :meth:`schedule` — drain the queue head-first, stopping on the first
                           blocked job to preserve FIFO ordering
      - :meth:`forecast` — annotate queued jobs with ``t_estimate`` via
                           forward simulation (optional planning hook)

    Helper methods (not base-class overrides):
      - :meth:`_sim_alloc`  — allocate one job in a simulation, advancing time
      - :meth:`_try_alloc`  — attempt a real allocation, handling exceptions
    """

    #: Default to 8 to match sched-simple behaviour: job-manager queues at
    #: most 8 pending alloc requests, bounding scheduler queue size and
    #: annotation overhead.  Override at load time with ``queue-depth=unlimited``
    #: if the full pending queue must be visible (e.g. for policy decisions).
    queue_depth = 8

    def __init__(self, h, *args):
        super().__init__(h, *args)

    # ------------------------------------------------------------------
    # Scheduler overrides
    # ------------------------------------------------------------------

    # ------------------------------------------------------------------
    # Scheduling loop
    # ------------------------------------------------------------------

    def schedule(self):
        """Drain the head of the queue as long as allocations succeed.

        Stops at the first job that cannot be allocated, preserving strict
        FIFO ordering: a lower-priority job must never start before the job
        ahead of it in the queue.
        """
        while self._queue:
            job = self._queue[0]
            if not self._try_alloc(job):
                break  # head blocked — stop to preserve FIFO ordering
            heapq.heappop(self._queue)

    def _sim_alloc(self, sim, jobid, rr, t_floor):
        """Allocate a resource request in a simulation pool.

        Starts from ``t_floor`` and repeatedly calls ``sim.alloc()``.  On
        :exc:`~flux.resource.InsufficientResources` it advances to the next
        expiration event, frees those resources, and retries, simulating the
        passage of time.

        Args:
            sim: A deep-copy resource pool used for simulation.
            jobid: ID of the job being simulated.
            rr: :class:`~flux.resource.Rv1Pool.ResourceRequest` for the job.
            t_floor: FIFO ordering floor — the estimated start time of the
                preceding job in the queue; this job cannot start earlier.

        Returns:
            The sim time at which the allocation succeeded (may equal
            ``t_floor`` if resources were immediately available), or ``None``
            if no future expirations remain to free enough resources.

        Raises:
            InfeasibleRequest: Propagated from ``sim.alloc()`` so the caller
                can skip the job, as :meth:`schedule` would.
        """
        t = t_floor
        while True:
            try:
                sim.alloc(jobid, rr)
                return t
            except InfeasibleRequest:
                raise  # propagate so caller can skip the job, as schedule() would
            except InsufficientResources:
                # Not enough resources now; advance to the next expiration.
                nxt = min(
                    (e for _, e in sim.job_end_times() if e > t),
                    default=None,
                )
                if nxt is None:
                    return None  # no future expirations — cannot estimate
                t = nxt
                for jid, end_time in list(sim.job_end_times()):
                    if 0 < end_time <= t:
                        sim.free(jid)

    def forecast(self):
        """Annotate queued jobs with ``t_estimate`` via forward simulation.

        Makes a deep copy of the resource pool and walks the queue in priority
        order, calling :meth:`_sim_alloc` for each job.  The FIFO ordering
        constraint is enforced via *t_floor*: job *k* cannot be estimated to
        start earlier than job *k-1*.  Estimation stops at the first job with
        no known duration (``--time-limit=0``) or whose request is permanently
        infeasible; remaining jobs receive ``None`` to clear stale estimates.

        Permanently infeasible jobs (those that would be denied in the real
        scheduling loop) are skipped: their annotation is cleared and
        estimation continues for the remaining jobs, without advancing the
        FIFO floor.  Estimation stops only at the first job with no known
        duration or with no future expirations to free enough resources.

        Complexity is O(N × M) where N is the number of queued jobs (bounded
        by ``queue_depth``, default 8) and M is the number of running jobs
        with known end times.  Each running job is freed from the simulation
        at most once across the entire walk.

        Annotations are suppressed when the value is unchanged since the last
        send, so overhead in steady state is minimal.
        """
        if not self._queue:
            return
        sim = self.resources.copy()

        t_prev = 0.0
        can_estimate = True
        for job in sorted(self._queue):
            rr = job.resource_request

            if can_estimate:
                try:
                    t = self._sim_alloc(sim, job.jobid, rr, t_prev)
                except InfeasibleRequest:
                    # Permanently infeasible: the real schedule() loop would deny
                    # and remove this job, then continue to the next one.  Do the
                    # same here — clear the annotation and keep estimating.
                    t_est = None
                    if job._last_annotation != t_est:
                        job._last_annotation = t_est
                        job.request.annotate({"sched": {"t_estimate": t_est}})
                    continue
                # Map sim time 0 (no advancement needed) to current wall clock.
                t_est = max(t, _time.time()) if t is not None else None
            else:
                # Cannot estimate jobs beyond an unknown-duration job in the chain.
                t_est = None

            if job._last_annotation != t_est:
                job._last_annotation = t_est
                job.request.annotate({"sched": {"t_estimate": t_est}})

            if not can_estimate or t is None:
                can_estimate = False
                continue

            # Advance the FIFO floor: the next job cannot start before this one.
            duration = rr.duration
            if duration > 0.0:
                sim.update_expiration(job.jobid, t + duration)
                t_prev = t
            else:
                can_estimate = False  # unknown end time — cannot chain further

    def _try_alloc(self, job):
        """Attempt to allocate resources for a job.

        Args:
            job: A :class:`~flux.scheduler.PendingJob` at the head of the
                queue.

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
        job.request.success(alloc, {"sched": {"resource_summary": summary}})
        return True


def mod_main(h, *args):
    FIFOScheduler(h, *args).run()
