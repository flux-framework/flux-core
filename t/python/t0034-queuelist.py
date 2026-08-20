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
from flux.queue import (
    QueueConf,
    QueueInfo,
    QueueList,
    queue_conf_from_config,
    queue_config_fetch,
)
from flux.resource import resource_list
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

    def test_003_queue_list_conf(self):
        # Exercise the queue-list RPC response directly: the conf object
        # carries each queue's effective config, with a virtual queue
        # inheriting its parent's requires. Queue order is not guaranteed,
        # so compare by set / by name.
        testconf = """
        [queues.debug]
        requires = ["debug"]

        [queues.batch]
        requires = ["batch"]

        [queues.expedite]
        parent = "batch"
        """
        self.fh.rpc("config.load", tomllib.loads(testconf)).get()

        resp = self.fh.rpc("job-manager.queue-list").get()
        self.assertEqual(set(resp["queues"]), {"debug", "batch", "expedite"})
        conf = resp["conf"]["queues"]
        self.assertEqual({q["name"] for q in conf}, {"debug", "batch", "expedite"})

        by_name = {q["name"]: q for q in conf}
        # real queue: own requires, no parent key
        self.assertEqual(by_name["batch"]["requires"], ["batch"])
        self.assertNotIn("parent", by_name["batch"])
        # virtual queue: inherits parent's requires and reports parent
        self.assertEqual(by_name["expedite"]["requires"], ["batch"])
        self.assertEqual(by_name["expedite"]["parent"], "batch")

        # A reconfigure is reflected in the response: drop 'debug' and
        # confirm the remaining queues are those configured.
        reconfig = """
        [queues.expedite]
        parent = "batch"

        [queues.batch]
        requires = ["batch"]
        """
        self.fh.rpc("config.load", tomllib.loads(reconfig)).get()
        resp = self.fh.rpc("job-manager.queue-list").get()
        self.assertEqual(set(resp["queues"]), {"expedite", "batch"})
        self.assertEqual(
            {q["name"] for q in resp["conf"]["queues"]},
            {"expedite", "batch"},
        )

    def test_0035_conf_matches_helper(self):
        # The queue_conf_from_config() helper (used as the fallback for
        # older brokers and by the hidden --config-file/--from-stdin
        # options) must reproduce the live job-manager 'conf' object, so the
        # two implementations do not drift. Include a global [policy] and
        # per-queue policy (including a virtual queue that overrides one key
        # and inherits another) so the conf's effective-policy and
        # default_queue fields are exercised, not just requires/parent.
        # Queue order is not guaranteed, so compare with queues sorted by
        # name.
        testconf = """
        [policy.limits]
        duration = "24h"

        [policy.jobspec.defaults.system]
        queue = "batch"

        [queues.debug]
        requires = ["debug"]

        [queues.batch]
        requires = ["batch"]
        policy.limits.duration = "8h"
        policy.limits.job-size.max.nnodes = 16

        [queues.expedite]
        parent = "batch"
        policy.limits.duration = "1h"
        """
        config = tomllib.loads(testconf)
        self.fh.rpc("config.load", config).get()

        def normalize(conf):
            conf = dict(conf)
            conf["queues"] = sorted(conf["queues"], key=lambda q: q["name"])
            return conf

        resp = self.fh.rpc("job-manager.queue-list").get()
        self.assertEqual(
            normalize(resp["conf"]), normalize(queue_conf_from_config(config))
        )

    def test_0036_queue_config_fetch(self):
        # queue_config_fetch() sends the request and its get() returns a
        # QueueConf built from the live job-manager.
        testconf = """
        [queues.batch]
        requires = ["batch"]
        [queues.expedite]
        parent = "batch"
        """
        self.fh.rpc("config.load", tomllib.loads(testconf)).get()

        conf = queue_config_fetch(self.fh).get()
        self.assertIsInstance(conf, QueueConf)
        self.assertEqual(sorted(conf), ["batch", "expedite"])
        # vqueue-resolved effective config from the live job-manager:
        self.assertEqual(conf.requires("expedite"), ["batch"])
        self.assertEqual(conf.parent("expedite"), "batch")

        # a second fetch reflects the same configuration:
        conf2 = queue_config_fetch(self.fh).get()
        self.assertEqual(sorted(conf2), sorted(conf))
        self.assertEqual(conf2.entries, conf.entries)

    def test_004_vqueue_stale_parent(self):
        # A QueueInfo parent (sourced from the queue-status RPC) missing
        # from the config (a separate RPC, so a config reload can race
        # the two) must raise, not fall back to the full instance
        # resource set or drop the parent's limits layer.
        resources = resource_list(self.fh).get()
        config = {
            "queues": {
                "batch": {"requires": ["batch"]},
            }
        }
        with self.assertRaises(ValueError) as ctx:
            QueueInfo(
                "expedite",
                config,
                resources,
                enabled=True,
                started=True,
                default=False,
                parent="gone",
            )
        self.assertIn("parent queue 'gone' is not configured", str(ctx.exception))

        # The limits/defaults lookup path fails the same way even when
        # constructed with a then-valid parent that a raced config lost:
        qinfo = QueueInfo(
            "expedite",
            config,
            resources,
            enabled=True,
            started=True,
            default=False,
            parent="batch",
        )
        qinfo.config = {"queues": {}}
        with self.assertRaises(ValueError):
            list(qinfo._config_entries())


if __name__ == "__main__":
    if rerun_under_flux(__flux_size()):
        from pycotap import TAPTestRunner

        unittest.main(testRunner=TAPTestRunner(), buffer=False)
