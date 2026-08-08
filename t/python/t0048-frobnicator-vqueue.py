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
from flux.job.frobnicator.plugins.constraints import QueueConfig
from flux.job.frobnicator.plugins.defaults import DefaultsConfig
from pycotap import TAPTestRunner


def defaults_config(system):
    return {"policy": {"jobspec": {"defaults": {"system": system}}}}


class TestConstraintsVQueue(unittest.TestCase):
    """RFC 33 virtual queue constraint inheritance in the frobnicator."""

    def test_vqueue_inherits_parent_requires(self):
        qc = QueueConfig(
            {
                "queues": {
                    "batch": {"requires": ["batch"]},
                    "expedite": {"parent": "batch"},
                }
            }
        )
        # The vqueue's effective properties are the parent's.
        self.assertEqual(qc.queue_properties("expedite"), ["batch"])
        self.assertEqual(qc.queue_properties("expedite"), qc.queue_properties("batch"))

    def test_nonvirtual_queue_uses_own_requires(self):
        qc = QueueConfig({"queues": {"batch": {"requires": ["batch"]}}})
        self.assertEqual(qc.queue_properties("batch"), ["batch"])

    def test_unknown_queue_returns_none(self):
        qc = QueueConfig({"queues": {"batch": {"requires": ["batch"]}}})
        self.assertIsNone(qc.queue_properties("nosuchqueue"))

    def test_vqueue_missing_parent_fails_closed(self):
        # A vqueue whose parent is not configured must raise, not
        # silently drop the parent's constraint (which would place the
        # job outside the parent's resource slice).
        qc = QueueConfig({"queues": {"expedite": {"parent": "nosuchqueue"}}})
        with self.assertRaises(ValueError):
            qc.queue_properties("expedite")


class TestDefaultsVQueue(unittest.TestCase):
    """RFC 33 virtual queue defaults inheritance in the frobnicator."""

    def test_vqueue_inherits_parent_defaults(self):
        dc = DefaultsConfig(
            {
                "queues": {
                    "batch": defaults_config({"duration": "1h"}),
                    "expedite": {"parent": "batch"},
                }
            }
        )
        self.assertEqual(dc.queue_defaults("expedite"), {"duration": "1h"})

    def test_vqueue_own_defaults_override_parent(self):
        expedite = defaults_config({"duration": "5m"})
        expedite["parent"] = "batch"
        dc = DefaultsConfig(
            {
                "queues": {
                    "batch": defaults_config({"duration": "1h", "queue": "batch"}),
                    "expedite": expedite,
                }
            }
        )
        # Own duration overrides the parent's; the parent's other keys
        # are still inherited beneath.
        eff = dc.queue_defaults("expedite")
        self.assertEqual(eff["duration"], "5m")
        self.assertEqual(eff["queue"], "batch")

    def test_vqueue_missing_parent_fails_closed(self):
        # DefaultsConfig validates every queue at construction, so a
        # vqueue with an unconfigured parent must raise there.
        with self.assertRaises(ValueError):
            DefaultsConfig({"queues": {"expedite": {"parent": "nosuchqueue"}}})


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
