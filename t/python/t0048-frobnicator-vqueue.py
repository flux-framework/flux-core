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
from flux.job import JobspecV1
from flux.job.frobnicator.plugins.constraints import apply_constraints
from flux.job.frobnicator.plugins.defaults import DefaultsConfig
from flux.queue import QueueConf
from pycotap import TAPTestRunner


def defaults_config(system):
    return {"policy": {"jobspec": {"defaults": {"system": system}}}}


def queue_conf(queues):
    """A QueueConf from a raw [queues] table (as the job-manager resolves it)."""
    return QueueConf.from_config({"queues": queues})


def jobspec(queue=None):
    js = JobspecV1.from_command(["hostname"])
    if queue is not None:
        js.queue = queue
    return js


def properties(js):
    return js.attributes["system"].get("constraints", {}).get("properties")


class TestConstraintsVQueue(unittest.TestCase):
    """The constraints frobnicator applies a queue's effective requires.

    RFC 33 virtual-queue inheritance is resolved by the job-manager, so the
    QueueConf entries already carry a vqueue's effective (parent's) requires.
    """

    def test_vqueue_inherits_parent_requires(self):
        qc = queue_conf(
            {
                "batch": {"requires": ["batch"]},
                "expedite": {"parent": "batch"},
            }
        )
        # The vqueue is constrained to the parent's required properties.
        js = jobspec("expedite")
        apply_constraints(qc, js)
        self.assertEqual(properties(js), ["batch"])

    def test_nonvirtual_queue_uses_own_requires(self):
        qc = queue_conf({"batch": {"requires": ["batch"]}})
        js = jobspec("batch")
        apply_constraints(qc, js)
        self.assertEqual(properties(js), ["batch"])

    def test_queue_without_requires_adds_no_constraint(self):
        qc = queue_conf({"batch": {}})
        js = jobspec("batch")
        apply_constraints(qc, js)
        self.assertIsNone(properties(js))

    def test_invalid_queue_fails(self):
        qc = queue_conf({"batch": {"requires": ["batch"]}})
        with self.assertRaises(ValueError):
            apply_constraints(qc, jobspec("nosuchqueue"))

    def test_no_queue_is_noop(self):
        qc = queue_conf({"batch": {"requires": ["batch"]}})
        js = jobspec()
        apply_constraints(qc, js)
        self.assertIsNone(properties(js))


class TestDefaultsVQueue(unittest.TestCase):
    """The defaults frobnicator reads effective defaults from QueueConf.

    RFC 33 virtual-queue inheritance is resolved by the job-manager, so the
    conf entries already carry a vqueue's effective defaults.
    """

    def test_vqueue_inherits_parent_defaults(self):
        dc = DefaultsConfig(
            queue_conf(
                {
                    "batch": defaults_config({"duration": "1h"}),
                    "expedite": {"parent": "batch"},
                }
            )
        )
        self.assertEqual(dc.queue_defaults("expedite"), {"duration": "1h"})

    def test_vqueue_own_defaults_override_parent(self):
        expedite = defaults_config({"duration": "5m"})
        expedite["parent"] = "batch"
        dc = DefaultsConfig(
            queue_conf(
                {
                    "batch": defaults_config({"duration": "1h", "queue": "batch"}),
                    "expedite": expedite,
                }
            )
        )
        # Own duration overrides the parent's; the parent's other keys
        # are still inherited beneath.
        eff = dc.queue_defaults("expedite")
        self.assertEqual(eff["duration"], "5m")
        self.assertEqual(eff["queue"], "batch")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
