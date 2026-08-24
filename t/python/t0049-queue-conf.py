#!/usr/bin/env python3

###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import unittest

import subflux  # noqa: F401 - To set up PYTHONPATH
from flux.queue import QueueConf, queue_conf_from_config
from pycotap import TAPTestRunner


class TestQueueConfFromConfig(unittest.TestCase):
    """queue_conf_from_config() builds the queue-list 'conf' object."""

    def test_empty_config(self):
        self.assertEqual(queue_conf_from_config({}), {"queues": []})
        self.assertEqual(queue_conf_from_config({"queues": {}}), {"queues": []})

    def test_all_queues_present(self):
        config = {
            "queues": {
                "debug": {"requires": ["debug"]},
                "batch": {"requires": ["batch"]},
                "expedite": {"parent": "batch"},
            }
        }
        conf = queue_conf_from_config(config)
        self.assertEqual(
            {q["name"] for q in conf["queues"]}, {"debug", "batch", "expedite"}
        )

    def test_real_queue_own_requires(self):
        conf = queue_conf_from_config({"queues": {"batch": {"requires": ["batch"]}}})
        entry = conf["queues"][0]
        self.assertEqual(entry, {"name": "batch", "requires": ["batch"]})
        self.assertNotIn("parent", entry)

    def test_queue_without_requires_omits_key(self):
        conf = queue_conf_from_config({"queues": {"plain": {}}})
        self.assertEqual(conf["queues"], [{"name": "plain"}])

    def test_vqueue_inherits_parent_requires(self):
        config = {
            "queues": {
                "batch": {"requires": ["batch"]},
                "expedite": {"parent": "batch"},
            }
        }
        by_name = {q["name"]: q for q in queue_conf_from_config(config)["queues"]}
        # The vqueue reports its parent and inherits the parent's requires.
        self.assertEqual(by_name["expedite"]["parent"], "batch")
        self.assertEqual(by_name["expedite"]["requires"], ["batch"])

    def test_vqueue_parent_without_requires_omits_key(self):
        config = {
            "queues": {
                "batch": {},
                "expedite": {"parent": "batch"},
            }
        }
        by_name = {q["name"]: q for q in queue_conf_from_config(config)["queues"]}
        self.assertNotIn("requires", by_name["expedite"])
        self.assertEqual(by_name["expedite"]["parent"], "batch")

    def test_missing_parent_fails_closed(self):
        # An unresolvable parent must raise (fail closed) rather than fall
        # through to reporting the vqueue as unconstrained.
        with self.assertRaises(ValueError) as ctx:
            queue_conf_from_config({"queues": {"expedite": {"parent": "nosuchqueue"}}})
        self.assertIn(
            "parent queue 'nosuchqueue' is not configured", str(ctx.exception)
        )


class TestQueueConf(unittest.TestCase):
    """QueueConf exposes effective per-queue configuration."""

    # A representative config: a real queue with its own requires, a virtual
    # queue inheriting its parent, and a queue with no requires.
    # QueueConf.from_config runs it through queue_conf_from_config, so the
    # per-queue requires entries are already vqueue-resolved.
    CONFIG = {
        "queues": {
            "batch": {"requires": ["batch"]},
            "expedite": {"parent": "batch"},
            "plain": {},
        },
    }

    def setUp(self):
        self.qc = QueueConf.from_config(self.CONFIG)

    def test_empty(self):
        qc = QueueConf({"queues": []})
        self.assertEqual(list(qc), [])

    def test_empty_conf_object(self):
        # An empty conf object (no "queues" key) is the anonymous-queue case.
        qc = QueueConf({})
        self.assertEqual(list(qc), [])
        self.assertEqual(len(qc), 0)
        self.assertFalse(qc)

    def test_len_and_truthiness(self):
        self.assertEqual(len(self.qc), 3)
        self.assertTrue(self.qc)

    def test_membership_and_iteration(self):
        self.assertIn("expedite", self.qc)
        self.assertNotIn("nosuchqueue", self.qc)
        self.assertEqual(set(self.qc), {"batch", "expedite", "plain"})

    def test_requires_vqueue_resolved(self):
        # A virtual queue reports its parent's requires (resolved server-side).
        self.assertEqual(self.qc.requires("expedite"), ["batch"])
        self.assertEqual(self.qc.requires("batch"), ["batch"])
        self.assertIsNone(self.qc.requires("plain"))

    def test_requires_anonymous_or_unknown(self):
        # name None (anonymous queue) or an unconfigured queue -> None.
        self.assertIsNone(self.qc.requires(None))
        self.assertIsNone(self.qc.requires("nosuchqueue"))

    def test_parent(self):
        self.assertEqual(self.qc.parent("expedite"), "batch")
        # A non-virtual or unconfigured queue has no parent.
        self.assertEqual(self.qc.parent("batch"), "")
        self.assertEqual(self.qc.parent("nosuchqueue"), "")

    def test_entries(self):
        # The raw name -> entry escape hatch.
        self.assertEqual(sorted(self.qc.entries), ["batch", "expedite", "plain"])
        self.assertEqual(
            self.qc.entries["batch"], {"name": "batch", "requires": ["batch"]}
        )


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
