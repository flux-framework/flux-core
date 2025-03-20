#!/usr/bin/env python3
###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import math
import unittest

try:
    import tomllib  # novermin
except ModuleNotFoundError:
    from flux.utils import tomli as tomllib

import flux
from flux.queue import QueueList
from subflux import rerun_under_flux


def __flux_size():
    return 2


class TestQueueList(unittest.TestCase):
    @classmethod
    def setUpClass(self):
        self.fh = flux.Flux()

    def test_001_anonymous(self):
        qlist = QueueList(self.fh)
        self.assertEqual(len(list(qlist)), 1)
        queue = qlist[""]
        self.assertEqual(queue.name, "")
        self.assertTrue(queue.enabled)
        self.assertEqual(queue.started, True)
        self.assertEqual(queue.limits.duration, math.inf)
        self.assertEqual(queue.limits.timelimit, math.inf)
        self.assertEqual(queue.limits.max.nnodes, math.inf)
        self.assertEqual(queue.limits.min.nnodes, 0)
        self.assertEqual(queue.is_default, True)
        self.assertEqual(queue.resources.all.nnodes, 2)
        self.assertEqual(queue.resources.up.nnodes, 2)
        self.assertEqual(queue.resources.free.nnodes, 2)
        self.assertEqual(queue.resources.allocated.nnodes, 0)

    def test_002_named(self):
        testconf = """
        [queues.batch]

        [queues.debug]
        policy.limits.duration = "1h"
        policy.limits.job-size.max.nnodes = 1
        policy.limits.job-size.max.ncores = 2

        [policy]
        limits.duration = "24h"
        jobspec.defaults.system.queue = "batch"
        jobspec.defaults.system.duration = "8h"
        """
        self.fh.rpc("config.load", tomllib.loads(testconf)).get()

        # queues of None, empty list, empty set should all return all queues.
        # Additionally check behavior of ["batch", "debug"]
        for queues in (None, [], set(), ["batch", "debug"]):
            qlist = QueueList(self.fh, queues)
            self.assertEqual(len(list(qlist)), 2)
            for queue in qlist:
                self.assertIn(queue.name, ("debug", "batch"))
                self.assertTrue(queue.enabled)
                self.assertFalse(queue.started)
                self.assertEqual(queue.resources.all.nnodes, 2)
                self.assertEqual(queue.resources.up.nnodes, 2)
                self.assertEqual(queue.resources.free.nnodes, 2)
                self.assertEqual(queue.resources.allocated.nnodes, 0)

        for queue in ("batch", "debug"):
            qlist = QueueList(self.fh, [queue])
            self.assertEqual(len(list(qlist)), 1)
            self.assertEqual(qlist[queue].name, queue)
            self.assertEqual(list(qlist)[0].name, queue)

        qlist = QueueList(self.fh)
        self.assertEqual(qlist.batch.resources.all.nnodes, 2)
        self.assertEqual(qlist.batch.name, "batch")
        self.assertEqual(qlist.batch.is_default, True)
        self.assertTrue(qlist.batch.enabled)
        self.assertFalse(qlist.batch.started)
        self.assertEqual(qlist.batch.limits.duration, 86400.0)
        self.assertEqual(qlist.batch.limits.timelimit, 86400.0)

        self.assertEqual(qlist.debug.resources.all.nnodes, 2)
        self.assertEqual(qlist.debug.name, "debug")
        self.assertFalse(qlist.debug.is_default)
        self.assertTrue(qlist.debug.enabled)
        self.assertFalse(qlist.debug.started)
        self.assertEqual(qlist.debug.limits.duration, 3600.0)
        self.assertEqual(qlist.debug.limits.timelimit, 3600.0)


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
