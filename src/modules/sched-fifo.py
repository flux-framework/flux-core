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

Implements a basic scheduler using the :class:`flux.scheduler.Scheduler`
base class.  Jobs are ordered by descending priority then ascending jobid
(FIFO within a priority level), matching the behaviour of sched-simple.

Resource allocation uses :meth:`~flux.scheduler.ResourcePool.alloc`.
The default resource pool (:class:`~flux.resource.Rv1Pool`) supports GPU
scheduling.  Load with ``resource-class=Rv1RlistPool`` to use the C-backed
implementation (no GPU support).

Load with::

    flux module load sched-fifo [queue-depth=8|unlimited]
    flux module load sched-fifo resource-class=Rv1RlistPool
    flux module load sched-fifo alloc-mode=worst-fit|best-fit|first-fit  # Rv1RlistPool only
"""

import errno
import heapq
import syslog
import time as _time

from _flux._core import lib
from flux.job import JobID
from flux.scheduler import Scheduler


class FIFOScheduler(Scheduler):
    """Priority-FIFO scheduler using the Scheduler base class.

    Module arguments::

        flux module load sched-fifo [queue-depth=8|unlimited]
        flux module load sched-fifo [alloc-mode=worst-fit|best-fit|first-fit]  # Rv1RlistPool only
        flux module load sched-fifo [resource-class=Rv1Pool|Rv1RlistPool]
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

    def hello(self, jobid, priority, userid, t_submit, R):
        """Mark a running job's resources as allocated in our resource pool."""
        super().hello(jobid, priority, userid, t_submit, R)
        self.handle.log(
            syslog.LOG_DEBUG, f"hello: {JobID(jobid).f58} alloc={R.dumps()}"
        )

    # ------------------------------------------------------------------
    # Scheduling loop
    # ------------------------------------------------------------------

    def schedule(self):
        """Drain the head of the queue as long as allocations succeed."""
        while self._queue:
            job = self._queue[0]
            if not self._try_alloc(job):
                break  # Head of queue blocked (ENOSPC) — stop here to preserve FIFO
            heapq.heappop(self._queue)

    def _sim_alloc(self, sim, jobid, rr, t_floor):
        """Allocate *rr* for *jobid* in *sim*, advancing time as needed.

        Starting from *t_floor* (the FIFO ordering floor), repeatedly tries
        ``sim.alloc()``; on ENOSPC advances to the next expiration event,
        frees those resources, and retries.

        Returns the sim time at which the allocation succeeded (may equal
        *t_floor* if resources were immediately available), or ``None`` if
        the request is permanently infeasible or no future expirations remain.
        """
        t = t_floor
        while True:
            try:
                sim.alloc(jobid, rr, mode=self._alloc_mode)
                return t
            except OSError as exc:
                if exc.errno != errno.ENOSPC:
                    return None  # permanently infeasible
                nxt = min(
                    (e for e, _ in sim._job_state.values() if e > t),
                    default=None,
                )
                if nxt is None:
                    return None  # no future expirations; cannot estimate
                t = nxt
                for jid, (end_time, _) in list(sim._job_state.items()):
                    if 0 < end_time <= t:
                        sim.free(jid)

    def forecast(self):
        """Annotate queued jobs with t_estimate via forward simulation.

        Makes a deep copy of the resource pool and walks the queue in priority
        order, calling :meth:`_sim_alloc` for each job.  The FIFO ordering
        constraint is enforced: job *k* cannot be estimated earlier than
        job *k-1*.  Estimation stops at the first job with no known duration
        (``--time-limit=0``) or whose request is permanently infeasible;
        remaining jobs are annotated with ``None`` to clear any stale estimate.
        The copy is discarded on return.

        Complexity is O(N × M) where N is the number of queued jobs (bounded
        by ``queue_depth``, default 8) and M is the number of running jobs
        with known end times.  Each running job is freed from the simulation
        at most once across the entire walk, so expiration scanning does not
        compound per queued job.

        Annotations are suppressed when the value is unchanged since the last
        send, so overhead in steady state is minimal.
        """
        if not self._queue:
            return
        sim = self.resources.deepcopy()

        t_prev = 0.0
        can_estimate = True
        for job in sorted(self._queue):
            rr = job.resource_request

            if can_estimate:
                t = self._sim_alloc(sim, job.jobid, rr, t_prev)
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

            # Fix the recorded end time and advance the FIFO floor.
            duration = rr.duration
            if duration > 0.0:
                sim.update_expiration(job.jobid, t + duration)
                t_prev = t
            else:
                can_estimate = False  # Unknown end time; cannot chain further

    def _try_alloc(self, job):
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

        # Set start/expiration times on the allocated resource set
        reactor = lib.flux_get_reactor(self.handle.handle)
        now = lib.flux_reactor_now(reactor)
        alloc.set_starttime(now)
        if rr.duration > 0.0:
            alloc.set_expiration(now + rr.duration)
        elif self.resources.expiration > 0.0:
            alloc.set_expiration(self.resources.expiration)

        summary = alloc.dumps()
        job.request.success(alloc.to_dict(), {"sched": {"resource_summary": summary}})
        return True


def mod_main(h, *args):
    FIFOScheduler(h, *args).run()
