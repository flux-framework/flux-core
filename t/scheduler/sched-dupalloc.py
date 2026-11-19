###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Buggy scheduler for testing flux-audit-alloc.

A FIFO scheduler that double books resources on request:

- A job submitted with ``--setattr=system.dup-alloc=1`` is granted the R
  of the most recent normal allocation, without touching the pool.
  The job-manager then holds two jobs with overlapping R.

- When such a duplicate job is freed, the scheduler frees the *original*
  job's resources from its pool, so the scheduler believes those
  resources are idle while the job-manager still has them allocated.
  Later frees of the original job are ignored.

- A job submitted with ``--setattr=system.slow-free=SECONDS`` has its
  resources freed from the pool only after a delay, as if the free
  request were in flight, so the job-manager and scheduler views
  differ briefly.

Load with::

    flux module load sched-dupalloc.py
"""

import heapq

from flux.resource import InfeasibleRequest, InsufficientResources
from flux.scheduler import Scheduler


class DupAllocScheduler(Scheduler):
    def __init__(self, h, *args):
        super().__init__(h, *args)
        self._last = None  # (jobid, R) of most recent normal allocation
        self._dups = {}  # duplicate jobid -> original jobid
        self._stolen = set()  # original jobids already freed from pool
        self._slow_free = {}  # jobid -> free delay in seconds
        self._timers = set()  # active delayed-free timer watchers

    def alloc(self, request, jobid, priority, userid, t_submit, jobspec):
        system = jobspec.get("attributes", {}).get("system", {})
        if system.get("slow-free"):
            self._slow_free[jobid] = float(system["slow-free"])
        if not system.get("dup-alloc"):
            super().alloc(request, jobid, priority, userid, t_submit, jobspec)
            return
        if self._last is None:
            request.deny("dup-alloc: no previous allocation to duplicate")
            return
        orig, R = self._last
        self._dups[jobid] = orig
        self.log.debug(f"dup-alloc: {jobid} gets R of {orig}")
        request.success(R)

    def free(self, jobid, R, final=False):
        if jobid in self._dups:
            if final:
                orig = self._dups.pop(jobid)
                self.resources.free(orig, None, True)
                self._stolen.add(orig)
            return
        if jobid in self._stolen:
            if final:
                self._stolen.discard(jobid)
            return
        if jobid in self._slow_free:
            self._delayed_free(jobid, R, final)
            return
        super().free(jobid, R, final)

    def _delayed_free(self, jobid, R, final):
        delay = self._slow_free.pop(jobid) if final else self._slow_free[jobid]

        def cb(h, watcher, revents, args):
            watcher.stop()
            self._timers.discard(watcher)
            super(DupAllocScheduler, self).free(jobid, R, final)
            self._request_schedule()

        watcher = self.handle.timer_watcher_create(delay, cb)
        self._timers.add(watcher)
        watcher.start()

    def schedule(self):
        while self._queue:
            job = self._queue[0]
            try:
                alloc = self.resources.alloc(job.jobid, job.resource_request)
            except InsufficientResources:
                break
            except InfeasibleRequest as exc:
                job.request.deny(str(exc))
            else:
                self._last = (job.jobid, alloc.to_dict())
                job.request.success(alloc)
            heapq.heappop(self._queue)
            yield


def mod_main(h, *args):
    DupAllocScheduler(h, *args).run()
