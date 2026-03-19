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

import json
import time
import unittest

import subflux  # noqa: F401 - for PYTHONPATH
from flux.resource import InfeasibleRequest, InsufficientResources
from flux.resource.Rv1Pool import _NO_MAX, ResourceRequest, Rv1Pool
from pycotap import TAPTestRunner


def rr(
    nnodes=0,
    nslots=1,
    slot_size=1,
    gpu_per_slot=0,
    duration=0.0,
    constraint=None,
    exclusive=False,
    nnodes_max=_NO_MAX,
    nslots_max=_NO_MAX,
):
    """Convenience wrapper to build a ResourceRequest for tests.

    Pass ``nnodes_max=None`` for an unbounded node range; omit it (or pass
    ``_NO_MAX``) for a fixed count.  Same for ``nslots_max``.
    """
    return ResourceRequest(
        nnodes,
        nslots,
        slot_size,
        gpu_per_slot,
        duration,
        constraint,
        exclusive,
        nnodes_max=nnodes_max,
        nslots_max=nslots_max,
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

# 2 nodes, 2 cores each — total cores (4) >= slot_size=3, but no single
# node has 3 cores; used to test per-node slot-capacity checks.
R_2x2 = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-1", "children": {"core": "0-1"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["node0", "node1"],
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

# 2 nodes, 4 cores + 1 GPU each — total GPUs (2) >= gpu_per_slot=2, but
# no single node has 2 GPUs; used to test per-node GPU-slot-capacity checks.
R_1gpu_per_node = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-1", "children": {"core": "0-3", "gpu": "0"}}],
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
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(1, rr(0, 1, 1))

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
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(3, rr(0, 9, 1))

    def test_alloc_multi_slot(self):
        a = self.pool.alloc(1, rr(0, 4, 2))  # 8 cores total
        self.assertEqual(a.count("core"), 8)

    def test_enospc_when_exhausted(self):
        self.pool.alloc(1, rr(0, 16, 1))  # fill all
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(2, rr(0, 1, 1))

    def test_eoverflow_too_many_cores(self):
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(0, 1, 100))

    def test_slot_size_fits_total_but_not_per_node(self):
        # slot_size=3 on a 2-node × 2-core cluster: total cores=4 >= 3, but
        # no single node has 3 cores, so the slot can never be filled.
        # Must raise InfeasibleRequest, not InsufficientResources (which
        # would leave the job stuck in the queue forever).
        p = Rv1Pool(R_2x2)
        with self.assertRaises(InfeasibleRequest):
            p.alloc(1, rr(0, 1, 3))

    def test_slot_size_exactly_fills_node(self):
        # slot_size equal to the per-node core count should succeed.
        p = Rv1Pool(R_2x2)
        a = p.alloc(1, rr(0, 1, 2))
        self.assertEqual(a.count("core"), 2)
        self.assertEqual(len(a._ranks), 1)

    def test_nslots_exceeds_per_node_slot_capacity(self):
        # 5 single-core slots requested, but 2-node × 2-core cluster
        # only provides 4 slots total.  InfeasibleRequest, not ENOSPC.
        p = Rv1Pool(R_2x2)
        with self.assertRaises(InfeasibleRequest):
            p.alloc(1, rr(0, 5, 1))

    def test_nnodes(self):
        # 2 nodes, 2 slots each, 1 core per slot
        a = self.pool.alloc(1, rr(2, 4, 1))
        self.assertEqual(a.count("core"), 4)
        # result must span exactly 2 ranks
        self.assertEqual(len(a._ranks), 2)

    def test_nnodes_enospc(self):
        # Fill 3 of 4 nodes; requesting all 4 should then be ENOSPC
        self.pool.alloc(1, rr(3, 12, 1))
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(2, rr(4, 4, 1))

    def test_nnodes_more_than_pool_eoverflow(self):
        # Asking for more nodes than exist is permanently infeasible
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(5, 5, 1))

    def test_nnodes_eoverflow(self):
        # need 2 nodes with 8 cores each, but nodes only have 4
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(2, 16, 4))

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

    def test_alloc_result_has_nodelist(self):
        # The shell requires execution.nodelist in the R returned for each job.
        a = self.pool.alloc(1, rr(0, 1, 1))
        d = a.to_dict()
        self.assertIn("nodelist", d["execution"])
        self.assertTrue(d["execution"]["nodelist"])


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
        self.assertEqual(len(info["allocated_cores"]), 4)


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
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(2, rr(0, 1, 1, gpu_per_slot=1))

    def test_gpu_eoverflow(self):
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(0, 1, 1, gpu_per_slot=10))

    def test_no_gpu_requested_ignores_gpu_nodes(self):
        # CPU-only request should succeed on GPU nodes
        a = self.pool.alloc(1, rr(0, 2, 1))
        self.assertEqual(a.count("core"), 2)

    def test_gpu_eoverflow_no_gpu_nodes(self):
        # CPU-only pool, GPU requested → InfeasibleRequest
        p = Rv1Pool(R_4x4)
        with self.assertRaises(InfeasibleRequest):
            p.alloc(1, rr(0, 1, 1, gpu_per_slot=1))

    def test_gpu_per_slot_fits_total_but_not_per_node(self):
        # gpu_per_slot=2, but each node has only 1 GPU.  Total GPUs (2)
        # satisfies the naive total check (2 >= 1*2=2), but no single node
        # can supply a 2-GPU slot.  Must raise InfeasibleRequest.
        p = Rv1Pool(R_1gpu_per_node)
        with self.assertRaises(InfeasibleRequest):
            p.alloc(1, rr(0, 1, 1, gpu_per_slot=2))

    def test_gpu_per_slot_exactly_fills_node(self):
        # gpu_per_slot equal to the per-node GPU count should succeed.
        p = Rv1Pool(R_1gpu_per_node)
        a = p.alloc(1, rr(0, 1, 1, gpu_per_slot=1))
        self.assertEqual(a.count("gpu"), 1)


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
        with self.assertRaises(InsufficientResources):
            p2.alloc(4, rr(0, 1, 1))

    def test_register_alloc_populates_job_state(self):
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))

        p2 = Rv1Pool(R_4x4)
        p2.register_alloc(99, a)
        self.assertIn(99, p2._job_state)

    def test_register_alloc_missing_rank_raises(self):
        # Simulates scheduler reload after a rank is excluded from config:
        # the job's R has rank 0, but the new pool has no rank 0.
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))  # allocate one node (rank 0)

        # Build a pool with rank 0 removed (as if excluded in config)
        from flux.idset import IDset

        p2 = Rv1Pool(R_4x4)
        p2.remove_ranks(IDset("0"))
        with self.assertRaises(ValueError):
            p2.register_alloc(1, a)

    def test_register_alloc_succeeds_when_rank_is_down(self):
        # During system instance bringup, compute nodes may be slow to come
        # online so the scheduler starts with all ranks present but marked
        # down.  register_alloc() must still succeed: down ranks are tracked
        # in _ranks (just with up=False) and are not considered missing.
        p1 = Rv1Pool(R_4x4)
        a = p1.alloc(1, rr(0, 4, 1))  # allocate one node

        p2 = Rv1Pool(R_4x4)
        p2.mark_down("all")  # all nodes down at hello time
        p2.register_alloc(1, a)  # must not raise
        self.assertIn(1, p2._job_state)

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
        with self.assertRaises(InfeasibleRequest):
            self.pool.check_feasibility(rr(0, 100, 1))

    def test_slot_size_fits_total_but_not_per_node(self):
        # slot_size=5 exceeds per-node capacity (4 cores each on R_4x4)
        # but fits the total (16).  Per-node check must still reject it.
        # Mirrors the C librlist test "too large of slot returns EOVERFLOW".
        with self.assertRaises(InfeasibleRequest):
            self.pool.check_feasibility(rr(0, 1, 5))

    def test_infeasible_too_many_nodes_eoverflow(self):
        with self.assertRaises(InfeasibleRequest):
            self.pool.check_feasibility(rr(5, 5, 1))

    def test_feasible_even_when_pool_exhausted(self):
        # Fully allocate the pool; feasibility checks a clean copy, so it
        # should still succeed for a satisfiable request.
        self.pool.alloc(1, rr(0, 16, 1))
        self.pool.check_feasibility(rr(0, 8, 1))  # should not raise

    def test_infeasible_with_property_constraint(self):
        pool = Rv1Pool(R_props)
        # "fast" nodes have only 4 cores total; requesting 10 is infeasible
        with self.assertRaises(InfeasibleRequest):
            pool.check_feasibility(rr(0, 10, 1, constraint={"properties": ["fast"]}))

    def test_feasible_with_property_constraint(self):
        pool = Rv1Pool(R_props)
        # "fast" nodes have 4 cores total; requesting 2 is fine
        pool.check_feasibility(rr(0, 2, 1, constraint={"properties": ["fast"]}))

    def test_does_not_modify_pool(self):
        # check_feasibility must not alter the pool's allocation state
        self.pool.alloc(1, rr(0, 8, 1))
        self.pool.check_feasibility(rr(0, 8, 1))
        # Still 8 cores allocated; only 8 remain
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(2, rr(0, 9, 1))


class TestRv1PoolCopy(unittest.TestCase):
    def setUp(self):
        self.pool = Rv1Pool(R_4x4)
        self.pool.alloc(1, rr(0, 4, 1))

    def test_copy_preserves_allocation(self):
        # copy() preserves alloc state so it can be used for simulation
        fresh = self.pool.copy()
        # 4 cores were already allocated in setUp; only 12 remain free
        with self.assertRaises(InsufficientResources):
            fresh.alloc(1, rr(0, 16, 1))
        a = fresh.alloc(2, rr(0, 12, 1))
        self.assertEqual(a.count("core"), 12)

    def test_copy_is_independent(self):
        # Mutations to the copy must not affect the original
        fresh = self.pool.copy()
        fresh.free(1)
        # original still has job 1 allocated
        self.assertEqual(self.pool.copy_allocated().count("core"), 4)

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

    def test_copy_allocated_preserves_properties(self):
        # Allocate on a rank with a property, then verify copy_allocated
        # carries the matching property (needed for sched.resource-status queue).
        pool = Rv1Pool(R_props)  # ranks 0-1 have "fast", 2-3 have "slow"
        pool.alloc(1, rr(1, 2, 1, constraint={"properties": ["fast"]}))
        ca = pool.copy_allocated()
        self.assertIn("fast", ca._properties)
        self.assertNotIn("slow", ca._properties)


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
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(0, 10, 1, constraint={"properties": ["fast"]}))

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


class TestRv1PoolPartialFree(unittest.TestCase):
    """Tests for partial-free (housekeeping) protocol.

    The scheduler allocation protocol may return a job's resources one or more
    ranks at a time before the final free.  Each partial free must:
      - release those ranks' cores/GPUs for immediate reuse
      - update _job_state to remove freed ranks (preventing double-free)
      - not remove the job from _job_state until final=True
    """

    def setUp(self):
        # 4-node pool, 4 cores each.  Allocate job 1 across all 4 ranks.
        self.pool = Rv1Pool(R_4x4)
        self.alloc = self.pool.alloc(1, rr(0, 16, 1))

    def _partial_R(self, ranks):
        """Return an Rv1Pool containing only *ranks* from the full allocation."""
        return self.alloc._copy_from_ranks(set(ranks))

    def test_partial_free_releases_resources(self):
        # Free ranks 0 and 1; their 8 cores should become available.
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)
        a2 = self.pool.alloc(2, rr(0, 8, 1))
        self.assertEqual(a2.count("core"), 8)

    def test_partial_free_updates_job_state(self):
        # After freeing ranks 0-1, _job_state should only track ranks 2-3.
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)
        _, remaining = self.pool._job_state[1]
        self.assertNotIn(0, remaining._ranks)
        self.assertNotIn(1, remaining._ranks)
        self.assertIn(2, remaining._ranks)
        self.assertIn(3, remaining._ranks)

    def test_partial_free_keeps_job_in_state(self):
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)
        self.assertIn(1, self.pool._job_state)

    def test_no_double_free_after_realloc(self):
        # Free ranks 0-1, reallocate them to job 2, then final-free job 1.
        # Job 2 must still hold ranks 0-1 after job 1's final free.
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)

        job2_alloc = self.pool.alloc(2, rr(0, 8, 1))
        self.assertEqual(job2_alloc.count("core"), 8)

        # Final free job 1 (ranks 2-3 remaining in _job_state)
        final_partial = self._partial_R([2, 3])
        self.pool.free(1, final_partial, final=True)

        # Job 2's cores (ranks 0-1) must still be allocated
        for rank in job2_alloc._ranks:
            info = self.pool._ranks[rank]
            self.assertTrue(
                len(info["allocated_cores"]) > 0,
                f"rank {rank} was double-freed",
            )

    def test_final_free_releases_remaining(self):
        # Partial free ranks 0-1, then final free should release ranks 2-3.
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)
        final_partial = self._partial_R([2, 3])
        self.pool.free(1, final_partial, final=True)
        self.assertNotIn(1, self.pool._job_state)
        # All cores should now be free
        a = self.pool.alloc(2, rr(0, 16, 1))
        self.assertEqual(a.count("core"), 16)

    def test_final_free_r_mismatch_raises(self):
        # Final free with wrong R (rank 0 only, but ranks 0-3 still tracked)
        # should raise ValueError.
        wrong_R = self._partial_R([0])
        with self.assertRaises(ValueError):
            self.pool.free(1, wrong_R, final=True)

    def test_partial_free_extra_rank_raises(self):
        # Freeing a rank not in the job's allocation must raise ValueError.
        partial = self._partial_R([0])
        self.pool.free(1, partial, final=False)
        # Now rank 0 is no longer in _job_state[1]; freeing it again is extra.
        with self.assertRaises(ValueError):
            self.pool.free(1, partial, final=False)

    def test_simulation_free_without_r_after_partial_free(self):
        # After a partial free, the simulation (R=None) must only free
        # the remaining tracked ranks, not the already-freed ones.
        partial = self._partial_R([0, 1])
        self.pool.free(1, partial, final=False)

        # Reallocate freed ranks to job 2
        job2_alloc = self.pool.alloc(2, rr(0, 8, 1))

        # Simulate what backfill does: free job 1 via R=None on a copy
        sim = self.pool.copy()
        sim.free(1)

        # In the simulation, ranks 2-3 (job 1 remainder) should now be free
        for rank in [2, 3]:
            self.assertEqual(len(sim._ranks[rank]["allocated_cores"]), 0)
        # Ranks 0-1 belong to job 2 in sim and must still be allocated
        for rank in job2_alloc._ranks:
            self.assertGreater(len(sim._ranks[rank]["allocated_cores"]), 0)


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

    def test_nslots_in_alloc_R(self):
        """nslots is stored in R execution dict after alloc (regression of #6632)."""
        p = Rv1Pool(R_4x4)
        a = p.alloc(1, rr(0, 4, 1))
        self.assertEqual(a.to_dict()["execution"]["nslots"], 4)

    def test_nslots_absent_without_alloc(self):
        """Pool not produced by alloc() has no nslots in R."""
        p = Rv1Pool(R_4x4)
        self.assertNotIn("nslots", p.to_dict()["execution"])

    def test_nslots_round_trip(self):
        """nslots survives to_dict() → Rv1Pool() round-trip."""
        p = Rv1Pool(R_4x4)
        a = p.alloc(1, rr(0, 4, 1))
        p2 = Rv1Pool(a.to_dict())
        self.assertEqual(p2.to_dict()["execution"]["nslots"], 4)


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
        return [Rv1Pool(R_4x4)]

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

    def test_exclusive_slot_jobspec_normalizes_nnodes(self):
        """slot-only jobspec with exclusive=True → nnodes set to nslots."""
        jobspec = {
            "version": 1,
            "resources": [
                {
                    "type": "slot",
                    "count": 3,
                    "exclusive": True,
                    "with": [{"type": "core", "count": 1}],
                }
            ],
            "tasks": [],
            "attributes": {"system": {"duration": 60.0}},
        }
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                req = pool.parse_resource_request(jobspec)
                self.assertEqual(req.nnodes, req.nslots)
                self.assertTrue(req.exclusive)

    def test_empty_constraints_dict_treated_as_none(self):
        """Empty constraints dict {} (sent by queue-update plugin) → constraint=None."""
        jobspec = {
            "version": 1,
            "resources": [
                {"type": "slot", "count": 1, "with": [{"type": "core", "count": 1}]}
            ],
            "tasks": [],
            "attributes": {"system": {"duration": 60.0, "constraints": {}}},
        }
        for pool in self._pools():
            with self.subTest(pool=type(pool).__name__):
                rr = pool.parse_resource_request(jobspec)
                self.assertIsNone(rr.constraint)


class TestParseCount(unittest.TestCase):
    """Tests for ResourceRequest._parse_count."""

    def test_integer(self):
        self.assertEqual(ResourceRequest._parse_count(4), (4, 4))

    def test_dict_bounded(self):
        self.assertEqual(ResourceRequest._parse_count({"min": 2, "max": 8}), (2, 8))

    def test_dict_unbounded(self):
        self.assertEqual(ResourceRequest._parse_count({"min": 2}), (2, None))

    def test_dict_explicit_default_operator_accepted(self):
        """Explicit operator='+' operand=1 is the implied default — accepted."""
        self.assertEqual(
            ResourceRequest._parse_count(
                {"min": 2, "max": 8, "operator": "+", "operand": 1}
            ),
            (2, 8),
        )

    def test_dict_unsupported_operator_raises(self):
        """operator='*' is not yet supported — raises ValueError."""
        with self.assertRaises(ValueError):
            ResourceRequest._parse_count(
                {"min": 2, "max": 8, "operator": "*", "operand": 2}
            )

    def test_dict_nonunit_operand_raises(self):
        """operator='+' with operand≠1 is not yet supported — raises ValueError."""
        with self.assertRaises(ValueError):
            ResourceRequest._parse_count(
                {"min": 2, "max": 8, "operator": "+", "operand": 4}
            )


class TestRangeAlloc(unittest.TestCase):
    """Tests for RFC 14 range count support in alloc()."""

    def test_nnodes_bounded_range_takes_max(self):
        """Request 2-4 nodes on 4-node pool → allocates all 4."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=2, nslots=2, nnodes_max=4))
        self.assertEqual(len(result._ranks), 4)

    def test_nslots_stored_in_R(self):
        """Actual allocated slot count is stored in R execution dict."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=2, nslots=2, nnodes_max=4))
        self.assertEqual(result.to_dict()["execution"]["nslots"], 4)

    def test_nnodes_bounded_range_capped_at_max(self):
        """Request 2-3 nodes on 4-node pool → allocates exactly 3."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=2, nslots=2, nnodes_max=3))
        self.assertEqual(len(result._ranks), 3)

    def test_nnodes_bounded_range_min_satisfied(self):
        """Request 2-4 nodes, only 2 up → allocates 2 (minimum)."""
        pool = Rv1Pool(R_4x4)
        pool.mark_down("2-3")
        result = pool.alloc(1, rr(nnodes=2, nslots=2, nnodes_max=4))
        self.assertEqual(len(result._ranks), 2)

    def test_nnodes_bounded_range_below_min_raises(self):
        """Request 3-4 nodes, only 2 up → InsufficientResources."""
        pool = Rv1Pool(R_4x4)
        pool.mark_down("2-3")
        with self.assertRaises(InsufficientResources):
            pool.alloc(1, rr(nnodes=3, nslots=3, nnodes_max=4))

    def test_nnodes_range_max_exceeds_pool_is_feasible(self):
        """Range min=2 max=8 on 4-node pool → allocates 4 (max capped at pool size)."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=2, nslots=2, nnodes_max=8))
        self.assertEqual(len(result._ranks), 4)

    def test_nnodes_unbounded_takes_all(self):
        """Unbounded nnodes_max (None) → allocates all available nodes."""
        pool = Rv1Pool(R_4x4)
        pool.mark_down("3")
        result = pool.alloc(1, rr(nnodes=1, nslots=1, nnodes_max=None))
        self.assertEqual(len(result._ranks), 3)

    def test_nslots_bounded_range_takes_max(self):
        """Request 2-8 slots on pool with 4 nodes × 4 cores → gets 16 slots."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=0, nslots=2, nslots_max=16))
        total = sum(len(r["cores"]) for r in result._ranks.values())
        self.assertEqual(total, 16)

    def test_nslots_bounded_range_capped_at_max(self):
        """Request 2-6 slots on 16-slot pool → gets exactly 6."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=0, nslots=2, nslots_max=6))
        total = sum(len(r["cores"]) for r in result._ranks.values())
        self.assertEqual(total, 6)

    def test_nslots_unbounded_takes_all(self):
        """Unbounded nslots_max (None) → drains all free slots."""
        pool = Rv1Pool(R_4x4)
        result = pool.alloc(1, rr(nnodes=0, nslots=1, nslots_max=None))
        total = sum(len(r["cores"]) for r in result._ranks.values())
        self.assertEqual(total, 16)  # 4 nodes × 4 cores

    def test_from_jobspec_range_dict(self):
        """from_jobspec with range dict sets nnodes/nnodes_max correctly."""
        jobspec = {
            "version": 1,
            "resources": [
                {
                    "type": "node",
                    "count": {"min": 2, "max": 4},
                    "with": [
                        {
                            "type": "slot",
                            "count": 1,
                            "with": [{"type": "core", "count": 2}],
                        }
                    ],
                }
            ],
            "tasks": [],
            "attributes": {"system": {"duration": 60.0}},
        }
        pool = Rv1Pool(R_4x4)
        req = pool.parse_resource_request(jobspec)
        self.assertEqual(req.nnodes, 2)
        self.assertEqual(req.nnodes_max, 4)
        self.assertEqual(req.nslots, 2)  # 1 slot/node × 2 min nodes
        self.assertEqual(req.nslots_max, 4)  # 1 slot/node × 4 max nodes

    def test_from_jobspec_unbounded_range(self):
        """from_jobspec with min-only count → nnodes_max is None (unbounded)."""
        jobspec = {
            "version": 1,
            "resources": [
                {
                    "type": "node",
                    "count": {"min": 1},
                    "with": [
                        {
                            "type": "slot",
                            "count": 1,
                            "with": [{"type": "core", "count": 1}],
                        }
                    ],
                }
            ],
            "tasks": [],
            "attributes": {"system": {"duration": 60.0}},
        }
        pool = Rv1Pool(R_4x4)
        req = pool.parse_resource_request(jobspec)
        self.assertEqual(req.nnodes, 1)
        self.assertIsNone(req.nnodes_max)
        result = pool.alloc(1, req)
        self.assertEqual(len(result._ranks), 4)

    def test_fixed_count_nnodes_max_equals_nnodes(self):
        """Fixed integer count → nnodes_max == nnodes (no range)."""
        pool = Rv1Pool(R_4x4)
        req = pool.parse_resource_request(
            {
                "version": 1,
                "resources": [
                    {
                        "type": "node",
                        "count": 2,
                        "with": [
                            {
                                "type": "slot",
                                "count": 1,
                                "with": [{"type": "core", "count": 1}],
                            }
                        ],
                    }
                ],
                "tasks": [],
                "attributes": {"system": {"duration": 60.0}},
            }
        )
        self.assertEqual(req.nnodes, 2)
        self.assertEqual(req.nnodes_max, 2)


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
