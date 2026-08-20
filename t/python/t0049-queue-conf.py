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

import copy
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

    def test_global_policy_at_top_level(self):
        # The global [policy] is carried at the top level (the effective
        # policy for the anonymous queue / a job with no queue).
        config = {"policy": {"limits": {"duration": "24h"}}}
        conf = queue_conf_from_config(config)
        self.assertEqual(conf["policy"], {"limits": {"duration": "24h"}})

    def test_default_queue_at_top_level(self):
        config = {"policy": {"jobspec": {"defaults": {"system": {"queue": "batch"}}}}}
        self.assertEqual(queue_conf_from_config(config)["default_queue"], "batch")

    def test_no_default_queue_omits_key(self):
        conf = queue_conf_from_config({"policy": {"limits": {"duration": "1h"}}})
        self.assertNotIn("default_queue", conf)

    def test_queue_policy_is_effective(self):
        # A queue's policy is fully effective: its own overrides the global
        # per key, and unset keys are inherited from the global.
        config = {
            "policy": {
                "limits": {"duration": "24h", "job-size": {"max": {"nnodes": 4}}}
            },
            "queues": {"batch": {"policy": {"limits": {"duration": "8h"}}}},
        }
        entry = queue_conf_from_config(config)["queues"][0]
        self.assertEqual(entry["policy"]["limits"]["duration"], "8h")
        self.assertEqual(entry["policy"]["limits"]["job-size"]["max"]["nnodes"], 4)

    def test_queue_without_own_policy_gets_global(self):
        # With a global policy, even a queue with no own policy has an
        # effective policy (the global).
        config = {
            "policy": {"limits": {"duration": "24h"}},
            "queues": {"batch": {"requires": ["batch"]}},
        }
        entry = queue_conf_from_config(config)["queues"][0]
        self.assertEqual(entry["policy"], {"limits": {"duration": "24h"}})

    def test_queue_policy_omitted_when_empty(self):
        # No policy at any layer (and no global) -> the key is omitted.
        conf = queue_conf_from_config({"queues": {"batch": {"requires": ["batch"]}}})
        self.assertNotIn("policy", conf["queues"][0])

    def test_empty_policy_table_omitted(self):
        # An explicitly empty policy table with no global is omitted.
        conf = queue_conf_from_config({"queues": {"batch": {"policy": {}}}})
        self.assertEqual(conf["queues"], [{"name": "batch"}])

    def test_vqueue_policy_merges_global_parent_own(self):
        # A vqueue's effective policy merges the global, then the parent's,
        # then its own, per key.
        config = {
            "policy": {"limits": {"job-size": {"min": {"nnodes": 1}}}},
            "queues": {
                "batch": {
                    "policy": {
                        "limits": {
                            "duration": "8h",
                            "job-size": {"max": {"nnodes": 16}},
                        }
                    }
                },
                "expedite": {
                    "parent": "batch",
                    "policy": {"limits": {"duration": "1h"}},
                },
            },
        }
        by_name = {q["name"]: q for q in queue_conf_from_config(config)["queues"]}
        self.assertEqual(
            by_name["expedite"]["policy"],
            {
                "limits": {
                    "duration": "1h",  # own
                    "job-size": {"max": {"nnodes": 16}, "min": {"nnodes": 1}},
                }  # max from parent, min from global
            },
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


class TestPolicyAliasing(unittest.TestCase):
    """queue_conf_from_config() must not alias or mutate its input.

    The per-queue effective-policy merge layers the global policy, the
    parent's policy, and the queue's own. A naive merge shares nested dicts
    by reference, so encoding one queue could corrupt another queue's
    entry, the global policy, or the caller's original config. These tests
    pin down that every layer is deep-copied.
    """

    # A config exercising deep nesting and every inheritance layer: a
    # global limit + default queue, a parent (batch) with its own nested
    # job-size limit, two sibling virtual queues of batch overriding
    # different keys, and a plain queue with no own policy.
    def _config(self):
        return {
            "policy": {
                "limits": {
                    "duration": "24h",
                    "job-size": {"min": {"nnodes": 1}},
                },
                "jobspec": {"defaults": {"system": {"queue": "batch"}}},
            },
            "queues": {
                "batch": {
                    "policy": {
                        "limits": {
                            "duration": "8h",
                            "job-size": {"max": {"nnodes": 16}},
                        }
                    }
                },
                "expedite": {
                    "parent": "batch",
                    "policy": {"limits": {"duration": "1h"}},
                },
                "urgent": {
                    "parent": "batch",
                    "policy": {"limits": {"job-size": {"max": {"nnodes": 4}}}},
                },
                "plain": {},
            },
        }

    def _by_name(self, config):
        return {q["name"]: q for q in queue_conf_from_config(config)["queues"]}

    def test_input_config_not_mutated(self):
        # The caller's config dict must be byte-for-byte unchanged.
        config = self._config()
        before = copy.deepcopy(config)
        queue_conf_from_config(config)
        self.assertEqual(config, before)

    def test_parent_entry_not_corrupted_by_vqueue(self):
        # Encoding expedite/urgent (which override batch's keys) must not
        # change batch's effective policy.
        by = self._by_name(self._config())
        self.assertEqual(by["batch"]["policy"]["limits"]["duration"], "8h")
        self.assertEqual(
            by["batch"]["policy"]["limits"]["job-size"]["max"]["nnodes"], 16
        )

    def test_sibling_vqueues_independent(self):
        # Two vqueues of the same parent overriding different keys must not
        # leak into each other.
        by = self._by_name(self._config())
        # expedite overrides duration, inherits batch's job-size.max=16
        self.assertEqual(by["expedite"]["policy"]["limits"]["duration"], "1h")
        self.assertEqual(
            by["expedite"]["policy"]["limits"]["job-size"]["max"]["nnodes"], 16
        )
        # urgent overrides job-size.max=4, inherits batch's duration=8h
        self.assertEqual(by["urgent"]["policy"]["limits"]["duration"], "8h")
        self.assertEqual(
            by["urgent"]["policy"]["limits"]["job-size"]["max"]["nnodes"], 4
        )

    def test_global_inherited_everywhere(self):
        # The global job-size.min=1 must appear in every queue's effective
        # policy, and the global's own copy must be intact at the top level.
        conf = queue_conf_from_config(self._config())
        by = {q["name"]: q for q in conf["queues"]}
        for name in ("batch", "expedite", "urgent", "plain"):
            self.assertEqual(
                by[name]["policy"]["limits"]["job-size"]["min"]["nnodes"],
                1,
                f"{name} lost the global job-size.min",
            )
        self.assertEqual(conf["policy"]["limits"]["duration"], "24h")
        self.assertEqual(conf["policy"]["limits"]["job-size"]["min"]["nnodes"], 1)

    def test_mutating_result_does_not_affect_siblings_or_global(self):
        # Mutating one queue's returned policy must not touch any other
        # queue's entry, the top-level global, or the input config.
        config = self._config()
        conf = queue_conf_from_config(config)
        by = {q["name"]: q for q in conf["queues"]}
        by["expedite"]["policy"]["limits"]["duration"] = "MUTATED"
        by["expedite"]["policy"]["limits"]["job-size"]["max"]["nnodes"] = -999
        self.assertEqual(by["batch"]["policy"]["limits"]["duration"], "8h")
        self.assertEqual(
            by["urgent"]["policy"]["limits"]["job-size"]["max"]["nnodes"], 4
        )
        self.assertEqual(conf["policy"]["limits"]["duration"], "24h")
        self.assertEqual(
            config["queues"]["batch"]["policy"]["limits"]["job-size"]["max"]["nnodes"],
            16,
        )

    def test_repeated_calls_identical(self):
        # Repeated encoding of the same config must yield identical output
        # (no cumulative corruption from a shared/aliased layer).
        config = self._config()
        first = queue_conf_from_config(config)
        second = queue_conf_from_config(config)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
