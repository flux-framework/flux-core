###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Minimal scheduler subclass using pool_class to bind TreePool.

Tests the ``if self.pool_class is not None`` path in
:meth:`~flux.scheduler.Scheduler._make_pool`.
"""

import heapq

from flux.resource import InfeasibleRequest, InsufficientResources
from flux.resource.TreePool import TreePool
from flux.scheduler import Scheduler


class TreeSubclassScheduler(Scheduler):
    pool_class = TreePool

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
    TreeSubclassScheduler(h, *args).run()
