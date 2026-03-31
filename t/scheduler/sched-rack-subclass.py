###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Minimal scheduler subclass using pool_class to bind RackPool.

Tests the ``if self.pool_class is not None`` path in
:meth:`~flux.scheduler.Scheduler._make_pool`.
"""

import heapq
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from RackPool import RackPool  # noqa: E402

from flux.resource import InfeasibleRequest, InsufficientResources
from flux.scheduler import Scheduler


class RackSubclassScheduler(Scheduler):
    pool_class = RackPool

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
                job.request.success(alloc)
            heapq.heappop(self._queue)
            yield


def mod_main(h, *args):
    RackSubclassScheduler(h, *args).run()
