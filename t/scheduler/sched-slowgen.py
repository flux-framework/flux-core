###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Slow generator scheduler for testing reactor liveness and abort/restart.

A minimal FIFO scheduler whose ``schedule()`` yields after every allocation
attempt, including blocked ones.  This maximises the number of reactor
iterations per pass so that tests can confirm the reactor remains live during
scheduling without relying on wall-clock timing.

The scheduler is intentionally simple: it stops at the first job that cannot
be immediately allocated (FIFO ordering), matching :class:`FIFOScheduler`
semantics.  There is no forecast implementation.

Load with::

    flux module load sched-slowgen.py

The ``flux module stats sched-slowgen`` output includes the base-class fields
(``sched_passes``, ``sched_yields``, etc.) which tests use to verify that
yielding is actually occurring.
"""

import heapq

from flux.resource import InfeasibleRequest, InsufficientResources
from flux.scheduler import Scheduler


class SlowGenScheduler(Scheduler):
    """FIFO scheduler that yields on every iteration for reactor-liveness testing."""

    #: Expose the full queue so that burst-submission tests see all jobs.
    queue_depth = "unlimited"

    def schedule(self):
        while self._queue:
            job = self._queue[0]
            try:
                alloc = self.resources.alloc(job.jobid, job.resource_request)
            except InsufficientResources:
                yield  # yield even when blocked so reactor stays live
                break
            except InfeasibleRequest as exc:
                job.request.deny(str(exc))
            else:
                job.request.success(alloc)
            heapq.heappop(self._queue)
            yield


def mod_main(h, *args):
    SlowGenScheduler(h, *args).run()
