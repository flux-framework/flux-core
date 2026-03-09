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

import errno
import json
import time
import unittest

import subflux  # noqa: F401 - for PYTHONPATH
from flux.resource.Rv1Pool import ResourceRequest, Rv1Pool
from pycotap import TAPTestRunner


def rr(
    nnodes=0,
    nslots=1,
    slot_size=1,
    gpu_per_slot=0,
    duration=0.0,
    constraint=None,
    exclusive=False,
):
    """Convenience wrapper to build a ResourceRequest for tests."""
    return ResourceRequest(
        nnodes, nslots, slot_size, gpu_per_slot, duration, constraint, exclusive
    )


# 4 nodes, 4 cores each (no GPUs)
R_4x4 = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-3", "children": {"core": "0-3"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["node0", "node1", "node2", "node3"],
    },
}

# 2 nodes: rank0 has 4 cores + 2 GPUs, rank1 has 4 cores + 2 GPUs
R_gpu = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-1", "children": {"core": "0-3", "gpu": "0-1"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["gpu0", "gpu1"],
    },
}

# 4 nodes with properties: rank0,1 have "fast"; rank2,3 have "slow"
R_props = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-3", "children": {"core": "0-1"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["node0", "node1", "node2", "node3"],
        "properties": {"fast": "0-1", "slow": "2-3"},
    },
}


class TestRv1PoolConstruct(unittest.TestCase):
    def test_from_dict(self):
        p = Rv1Pool(R_4x4)
        self.assertEqual(p.count("core"), 16)

    def test_from_string(self):
        p = Rv1Pool(json.dumps(R_4x4))
        self.assertEqual(p.count("core"), 16)

    def test_bad_type_raises(self):
        with self.assertRaises(TypeError):
            Rv1Pool(42)

    def test_gpu_parsed(self):
        p = Rv1Pool(R_gpu)
        self.assertEqual(p.count("core"), 8)
        self.assertEqual(p.count("gpu"), 4)

    def test_unknown_resource_count_zero(self):
        p = Rv1Pool(R_4x4)
        self.assertEqual(p.count("banana"), 0)

    def test_supports_gpu_class_attr(self):
        self.assertTrue(Rv1Pool.supports_gpu)

    def test_expiration(self):
        R = dict(R_4x4)
        R["execution"] = dict(R_4x4["execution"], expiration=1234.5)
        p = Rv1Pool(R)
        self.assertAlmostEqual(p.expiration, 1234.5)


class TestRv1PoolUpDown(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)
        self.pool.mark_down("all")

    def test_all_down_count_still_total(self):
        # count() totals all ranks regardless of up/down
        self.assertEqual(self.pool.count("core"), 16)

    def test_mark_up_single(self):
        self.pool.mark_up("0")
        alloc = self.pool.alloc(1, rr(0, 1, 1))
        self.assertEqual(alloc.count("core"), 1)

    def test_mark_up_all(self):
        self.pool.mark_up("all")
        alloc = self.pool.alloc(1, rr(0, 4, 1))
        self.assertEqual(alloc.count("core"), 4)

    def test_all_down_enospc(self):
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(0, 1, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_copy_down(self):
        down = self.pool.copy_down()
        self.assertEqual(down.count("core"), 16)

    def test_copy_down_after_partial_up(self):
        self.pool.mark_up("0-1")
        down = self.pool.copy_down()
        self.assertEqual(down.count("core"), 8)  # ranks 2 and 3


class TestRv1PoolExpiration(unittest.TestCase):
    def test_set_get(self):
        p = Rv1Pool(R_4x4)
        p.expiration = 9999.0
        self.assertAlmostEqual(p.expiration, 9999.0)

    def test_set_expiration_method(self):
        p = Rv1Pool(R_4x4)
        p.set_expiration(5000.0)
        self.assertAlmostEqual(p.expiration, 5000.0)

    def test_set_starttime_method(self):
        p = Rv1Pool(R_4x4)
        p.set_starttime(1000.0)
        d = p.to_dict()
        self.assertAlmostEqual(d["execution"]["starttime"], 1000.0)


class TestRv1PoolAlloc(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)

    def test_alloc_one_slot(self):
        a = self.pool.alloc(1, rr(0, 1, 1))
        self.assertEqual(a.count("core"), 1)

    def test_alloc_reduces_free(self):
        self.pool.alloc(1, rr(0, 4, 1))
        # 4 cores gone; 12 remain
        a2 = self.pool.alloc(2, rr(0, 4, 1))
        self.assertEqual(a2.count("core"), 4)
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(3, rr(0, 9, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_alloc_multi_slot(self):
        a = self.pool.alloc(1, rr(0, 4, 2))  # 8 cores total
        self.assertEqual(a.count("core"), 8)

    def test_enospc_when_exhausted(self):
        self.pool.alloc(1, rr(0, 16, 1))  # fill all
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(2, rr(0, 1, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_eoverflow_too_many_cores(self):
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(0, 1, 100))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_mode_none_ok(self):
        a = self.pool.alloc(1, rr(0, 1, 1), mode=None)
        self.assertEqual(a.count("core"), 1)

    def test_mode_worst_fit_ok(self):
        a = self.pool.alloc(1, rr(0, 1, 1), mode="worst-fit")
        self.assertEqual(a.count("core"), 1)

    def test_mode_unsupported_raises(self):
        with self.assertRaises(OSError):
            self.pool.alloc(1, rr(0, 1, 1), mode="best-fit")

    def test_nnodes(self):
        # 2 nodes, 2 slots each, 1 core per slot
        a = self.pool.alloc(1, rr(2, 4, 1))
        self.assertEqual(a.count("core"), 4)
        # result must span exactly 2 ranks
        self.assertEqual(len(a._ranks), 2)

    def test_nnodes_enospc(self):
        # Fill 3 of 4 nodes; requesting all 4 should then be ENOSPC
        self.pool.alloc(1, rr(3, 12, 1))
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(2, rr(4, 4, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_nnodes_more_than_pool_eoverflow(self):
        # Asking for more nodes than exist is permanently infeasible
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(5, 5, 1))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_nnodes_eoverflow(self):
        # need 2 nodes with 8 cores each, but nodes only have 4
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(2, 16, 4))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_jobid_tracked_in_job_state(self):
        self.pool.alloc(42, rr(0, 1, 1))
        self.assertIn(42, self.pool._job_state)

    def test_alloc_with_duration_records_end_time(self):
        before = time.time()
        self.pool.alloc(1, rr(0, 1, 1, duration=3600.0))
        end_time, _ = self.pool._job_state[1]
        self.assertGreater(end_time, before + 3600.0 - 1)
        self.assertLess(end_time, before + 3600.0 + 10)

    def test_alloc_no_duration_end_time_zero(self):
        # No duration, no pool expiration → end_time is 0.0
        self.pool.alloc(1, rr(0, 1, 1, duration=0.0))
        end_time, _ = self.pool._job_state[1]
        self.assertEqual(end_time, 0.0)

    def test_alloc_no_duration_uses_pool_expiration(self):
        self.pool.set_expiration(9999.0)
        self.pool.alloc(1, rr(0, 1, 1, duration=0.0))
        end_time, _ = self.pool._job_state[1]
        self.assertAlmostEqual(end_time, 9999.0)


class TestRv1PoolWorstFit(unittest.TestCase):
    """Verify worst-fit picks the node with the most free cores."""

    def _make_uneven_pool(self):
        """2-node pool: rank0 has 4 cores, rank1 has 2 cores."""
        R = {
            "version": 1,
            "execution": {
                "R_lite": [
                    {"rank": "0", "children": {"core": "0-3"}},
                    {"rank": "1", "children": {"core": "0-1"}},
                ],
                "starttime": 0,
                "expiration": 0,
                "nodelist": ["big", "small"],
            },
        }
        return Rv1Pool(R)

    def test_picks_larger_node_first(self):
        p = self._make_uneven_pool()
        # Allocate 1 core; worst-fit should pick rank0 (4 free > 2 free)
        a = p.alloc(1, rr(0, 1, 1))
        self.assertIn(0, a._ranks)
        self.assertNotIn(1, a._ranks)


class TestRv1PoolExclusive(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)

    def test_exclusive_reserves_whole_node(self):
        # Request 1 slot exclusively — should reserve all 4 cores of that node
        a = self.pool.alloc(1, rr(1, 1, 1, exclusive=True))
        self.assertEqual(a.count("core"), 4)
        self.assertEqual(len(a._ranks), 1)

    def test_exclusive_blocks_second_alloc_on_same_node(self):
        a = self.pool.alloc(1, rr(1, 1, 1, exclusive=True))
        rank = next(iter(a._ranks))
        info = self.pool._ranks[rank]
        # That node should now show all 4 cores allocated
        self.assertEqual(info["nallocated"], 4)


class TestRv1PoolGPU(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_gpu)

    def test_alloc_with_gpu(self):
        a = self.pool.alloc(1, rr(0, 2, 1, gpu_per_slot=1))
        self.assertEqual(a.count("core"), 2)
        self.assertEqual(a.count("gpu"), 2)

    def test_gpu_enospc(self):
        # Use up all GPUs first
        self.pool.alloc(1, rr(0, 4, 1, gpu_per_slot=1))
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(2, rr(0, 1, 1, gpu_per_slot=1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_gpu_eoverflow(self):
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(0, 1, 1, gpu_per_slot=10))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_no_gpu_requested_ignores_gpu_nodes(self):
        # CPU-only request should succeed on GPU nodes
        a = self.pool.alloc(1, rr(0, 2, 1))
        self.assertEqual(a.count("core"), 2)

    def test_gpu_eoverflow_no_gpu_nodes(self):
        # CPU-only pool, GPU requested → EOVERFLOW
        p = Rv1Pool(R_4x4)
        with self.assertRaises(OSError) as cm:
            p.alloc(1, rr(0, 1, 1, gpu_per_slot=1))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)


class TestRv1PoolFree(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)

    def test_free_restores_resources(self):
        self.pool.alloc(1, rr(0, 16, 1))
        self.pool.free(1)
        a2 = self.pool.alloc(2, rr(0, 16, 1))
        self.assertEqual(a2.count("core"), 16)

    def test_free_removes_from_job_state(self):
        self.pool.alloc(1, rr(0, 1, 1))
        self.assertIn(1, self.pool._job_state)
        self.pool.free(1)
        self.assertNotIn(1, self.pool._job_state)

    def test_free_tolerant(self):
        # free() should not raise even when jobid is unknown
        self.pool.alloc(1, rr(0, 1, 1))
        self.pool.free(1)
        self.pool.free(1)  # second free — should not raise


class TestRv1PoolRegisterAlloc(unittest.TestCase):
    def test_register_alloc_marks_busy(self):
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))  # allocate 4 cores

        # Build a fresh pool and replay the allocation via register_alloc
        p2 = Rv1Pool(R_4x4)
        p2.register_alloc(2, a)

        # p2 should now have 4 cores allocated; only 12 left
        alloc2 = p2.alloc(3, rr(0, 12, 1))
        self.assertEqual(alloc2.count("core"), 12)
        with self.assertRaises(OSError) as cm:
            p2.alloc(4, rr(0, 1, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)

    def test_register_alloc_populates_job_state(self):
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))

        p2 = Rv1Pool(R_4x4)
        p2.register_alloc(99, a)
        self.assertIn(99, p2._job_state)

    def test_register_alloc_uses_r_expiration(self):
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))
        a.set_expiration(5000.0)

        p2 = Rv1Pool(R_4x4)
        p2.register_alloc(10, a)
        end_time, _ = p2._job_state[10]
        self.assertAlmostEqual(end_time, 5000.0)


class TestRv1PoolUpdateExpiration(unittest.TestCase):
    def test_update_expiration_changes_end_time(self):
        p = Rv1Pool(R_4x4)
        p.alloc(1, rr(0, 1, 1, duration=3600.0))
        p.update_expiration(1, 9999.0)
        end_time, _ = p._job_state[1]
        self.assertAlmostEqual(end_time, 9999.0)

    def test_update_expiration_unknown_jobid_is_noop(self):
        p = Rv1Pool(R_4x4)
        # Should not raise for unknown jobid
        p.update_expiration(999, 5000.0)


class TestRv1PoolCheckFeasibility(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)

    def test_feasible_request_succeeds(self):
        # 8 cores fits in 4-node × 4-core pool
        self.pool.check_feasibility(rr(0, 8, 1))

    def test_infeasible_too_many_cores_eoverflow(self):
        with self.assertRaises(OSError) as cm:
            self.pool.check_feasibility(rr(0, 100, 1))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_infeasible_too_many_nodes_eoverflow(self):
        with self.assertRaises(OSError) as cm:
            self.pool.check_feasibility(rr(5, 5, 1))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_feasible_even_when_pool_exhausted(self):
        # Fully allocate the pool; feasibility checks a clean copy, so it
        # should still succeed for a satisfiable request.
        self.pool.alloc(1, rr(0, 16, 1))
        self.pool.check_feasibility(rr(0, 8, 1))  # should not raise

    def test_infeasible_with_property_constraint(self):
        pool = Rv1Pool(R_props)
        # "fast" nodes have only 4 cores total; requesting 10 is infeasible
        with self.assertRaises(OSError) as cm:
            pool.check_feasibility(rr(0, 10, 1, constraint={"properties": ["fast"]}))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_feasible_with_property_constraint(self):
        pool = Rv1Pool(R_props)
        # "fast" nodes have 4 cores total; requesting 2 is fine
        pool.check_feasibility(rr(0, 2, 1, constraint={"properties": ["fast"]}))

    def test_does_not_modify_pool(self):
        # check_feasibility must not alter the pool's allocation state
        self.pool.alloc(1, rr(0, 8, 1))
        self.pool.check_feasibility(rr(0, 8, 1))
        # Still 8 cores allocated; only 8 remain
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(2, rr(0, 9, 1))
        self.assertEqual(cm.exception.errno, errno.ENOSPC)


class TestRv1PoolCopy(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)
        self.pool.alloc(1, rr(0, 4, 1))

    def test_copy_clears_allocation(self):
        fresh = self.pool.copy()
        a = fresh.alloc(1, rr(0, 16, 1))
        self.assertEqual(a.count("core"), 16)

    def test_copy_preserves_up_down(self):
        self.pool.mark_down("3")
        fresh = self.pool.copy()
        down = fresh.copy_down()
        self.assertIn(3, down._ranks)

    def test_copy_allocated(self):
        ca = self.pool.copy_allocated()
        self.assertEqual(ca.count("core"), 4)

    def test_copy_allocated_empty_when_none(self):
        p = Rv1Pool(R_4x4)
        ca = p.copy_allocated()
        self.assertEqual(ca.count("core"), 0)


class TestRv1PoolConstraints(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_props)

    def test_property_constraint(self):
        a = self.pool.alloc(1, rr(0, 2, 1, constraint={"properties": ["fast"]}))
        # Both allocated slots must be on ranks 0 or 1
        for rank in a._ranks:
            self.assertIn(rank, {0, 1})

    def test_property_constraint_eoverflow(self):
        # "fast" nodes have only 4 cores total (2 ranks × 2 cores)
        with self.assertRaises(OSError) as cm:
            self.pool.alloc(1, rr(0, 10, 1, constraint={"properties": ["fast"]}))
        self.assertEqual(cm.exception.errno, errno.EOVERFLOW)

    def test_hostlist_constraint(self):
        a = self.pool.alloc(1, rr(0, 1, 1, constraint={"hostlist": "node0"}))
        self.assertIn(0, a._ranks)

    def test_ranks_constraint(self):
        a = self.pool.alloc(1, rr(0, 1, 1, constraint={"ranks": "2-3"}))
        for rank in a._ranks:
            self.assertIn(rank, {2, 3})

    def test_not_constraint(self):
        a = self.pool.alloc(
            1,
            rr(
                0,
                1,
                1,
                constraint={"not": [{"properties": ["slow"]}]},
            ),
        )
        for rank in a._ranks:
            self.assertIn(rank, {0, 1})

    def test_constraint_as_json_string(self):
        a = self.pool.alloc(
            1,
            rr(
                0,
                1,
                1,
                constraint=json.dumps({"properties": ["slow"]}),
            ),
        )
        for rank in a._ranks:
            self.assertIn(rank, {2, 3})


class TestRv1PoolRemoveRanks(unittest.TestCase):
    def test_remove_reduces_count(self):
        from flux.idset import IDset

        p = Rv1Pool(R_4x4)
        p.remove_ranks(IDset("2-3"))
        self.assertEqual(p.count("core"), 8)

    def test_remove_prevents_alloc_on_removed(self):
        from flux.idset import IDset

        p = Rv1Pool(R_4x4)
        p.remove_ranks(IDset("0-2"))
        a = p.alloc(1, rr(0, 4, 1))
        # Only rank3 remains; can only get 4 cores
        self.assertEqual(a.count("core"), 4)
        self.assertEqual(list(a._ranks.keys()), [3])


class TestRv1PoolEncode(unittest.TestCase):
    def test_encode_is_valid_json(self):
        p = Rv1Pool(R_4x4)
        s = p.encode()
        d = json.loads(s)
        self.assertEqual(d["version"], 1)
        self.assertIn("R_lite", d["execution"])

    def test_to_dict_matches_encode(self):
        p = Rv1Pool(R_4x4)
        self.assertEqual(p.to_dict(), json.loads(p.encode()))

    def test_round_trip(self):
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))
        p2 = Rv1Pool(a.to_dict())
        self.assertEqual(p2.count("core"), 4)

    def test_encode_includes_gpu(self):
        p = Rv1Pool(R_gpu)
        a = p.alloc(1, rr(0, 1, 1, gpu_per_slot=1))
        d = a.to_dict()
        children = d["execution"]["R_lite"][0]["children"]
        self.assertIn("gpu", children)

    def test_encode_includes_properties(self):
        p = Rv1Pool(R_props)
        d = p.to_dict()
        self.assertIn("properties", d["execution"])
        self.assertIn("fast", d["execution"]["properties"])

    def test_encode_expiration_starttime(self):
        p = Rv1Pool(R_4x4)
        p.set_starttime(100.0)
        p.set_expiration(200.0)
        d = p.to_dict()
        self.assertAlmostEqual(d["execution"]["starttime"], 100.0)
        self.assertAlmostEqual(d["execution"]["expiration"], 200.0)


class TestRv1PoolDumps(unittest.TestCase):
    def test_single_rank(self):
        p = Rv1Pool(R_4x4)
        a = p.alloc(1, rr(1, 1, 1, exclusive=True))
        s = a.dumps()
        # Should look like "rank<N>/core[0-3]"
        self.assertRegex(s, r"^rank\d+/core")

    def test_multi_rank(self):
        p = Rv1Pool(R_4x4)
        a = p.alloc(1, rr(0, 8, 1))
        s = a.dumps()
        self.assertIn("rank", s)
        self.assertIn("core", s)

    def test_gpu_in_dumps(self):
        p = Rv1Pool(R_gpu)
        a = p.alloc(1, rr(0, 1, 1, gpu_per_slot=1))
        s = a.dumps()
        self.assertIn("gpu", s)


class TestFromJobspec(unittest.TestCase):
    """Test from_jobspec / parse_resource_request for both pool classes."""

    # Minimal valid V1 jobspec: slot -> core, attributes.system.duration present
    VALID_V1 = {
        "version": 1,
        "resources": [
            {"type": "slot", "count": 2, "with": [{"type": "core", "count": 1}]}
        ],
        "tasks": [],
        "attributes": {"system": {"duration": 60.0}},
    }
    # V1 jobspec missing attributes.system entirely
    NO_SYSTEM = {
        "version": 1,
        "resources": [
            {"type": "slot", "count": 1, "with": [{"type": "core", "count": 1}]}
        ],
        "tasks": [],
        "attributes": {},
    }
    # V1 jobspec with attributes.system but missing duration
    NO_DURATION = {
        "version": 1,
        "resources": [
            {"type": "slot", "count": 1, "with": [{"type": "core", "count": 1}]}
        ],
        "tasks": [],
        "attributes": {"system": {}},
    }
    # Non-V1 jobspec with no attributes.system (should be accepted)
    NON_V1_NO_SYSTEM = {
        "version": 9999,
        "resources": [
            {"type": "slot", "count": 1, "with": [{"type": "core", "count": 1}]}
        ],
        "tasks": [],
        "attributes": {},
    }

    def _pools(self):
        """Return one instance of each pool class for parametrised tests."""
        from flux.resource.Rv1RlistPool import Rv1RlistPool

        return [Rv1Pool(R_4x4), Rv1RlistPool(R_4x4)]

    def test_valid_v1_parses(self):
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                rr = pool.parse_resource_request(self.VALID_V1)
                self.assertEqual(rr.nslots, 2)
                self.assertAlmostEqual(rr.duration, 60.0)

    def test_v1_missing_system_raises(self):
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                with self.assertRaises(ValueError) as ctx:
                    pool.parse_resource_request(self.NO_SYSTEM)
                self.assertIn("system", str(ctx.exception))

    def test_v1_missing_duration_raises(self):
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                with self.assertRaises(ValueError) as ctx:
                    pool.parse_resource_request(self.NO_DURATION)
                self.assertIn("duration", str(ctx.exception))

    def test_non_v1_missing_system_accepted(self):
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                rr = pool.parse_resource_request(self.NON_V1_NO_SYSTEM)
                self.assertAlmostEqual(rr.duration, 0.0)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
