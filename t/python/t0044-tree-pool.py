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

"""Unit tests for TreePool: NUMA-affinity GPU+core allocation."""

import copy
import json
import unittest

import subflux  # noqa: F401,E402 - configures PYTHONPATH for flux imports
from flux.idset import IDset  # noqa: E402
from flux.resource import InfeasibleRequest, InsufficientResources  # noqa: E402
from flux.resource.ResourceCount import ResourceCount  # noqa: E402
from flux.resource.Rv1Pool import ResourceRequest  # noqa: E402
from flux.resource.TreePool import TreePool  # noqa: E402
from pycotap import TAPTestRunner  # noqa: E402

_UNSET = object()


def rr(
    nnodes=0,
    nslots=1,
    slot_size=1,
    gpu_per_slot=0,
    exclusive=False,
    nnodes_max=_UNSET,
    nslots_max=_UNSET,
):
    """Build a ResourceRequest from flat parameters."""
    if nnodes_max is _UNSET:
        nnodes_max = nnodes
    if nslots_max is _UNSET:
        nslots_max = nslots
    if nnodes > 0:
        spn = nslots // nnodes
        node_count = ResourceCount(nnodes, nnodes_max)
        slot_count = ResourceCount(spn, spn)
    else:
        node_count = None
        slot_count = ResourceCount(nslots, nslots_max)
    return ResourceRequest(
        node_count, slot_count, slot_size, gpu_per_slot, 0.0, None, exclusive, None
    )


# 2 nodes, 8 cores (0-7) + 2 GPUs (0-1) each.
# 2 sockets per node, each with 1 NUMA child:
#   socket 0: cores 0-3, gpu 0
#   socket 1: cores 4-7, gpu 1
_SOCKETS_2 = [
    {"numa": [{"cores": "0-3", "gpus": "0"}]},
    {"numa": [{"cores": "4-7", "gpus": "1"}]},
]
R_2NODE = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0-1", "children": {"core": "0-7", "gpu": "0-1"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["gpu0", "gpu1"],
    },
    "scheduling": {
        "writer": "TreePool",
        "children": [{"ranks": "0-1", "topo": {"socket": _SOCKETS_2}}],
    },
}

# 1 node, 2 sockets, each with 4 cores + 1 GPU.
R_1NODE = {
    "version": 1,
    "execution": {
        "R_lite": [{"rank": "0", "children": {"core": "0-7", "gpu": "0-1"}}],
        "starttime": 0,
        "expiration": 0,
        "nodelist": ["gpu0"],
    },
    "scheduling": {
        "writer": "TreePool",
        "children": [{"ranks": "0", "topo": {"socket": _SOCKETS_2}}],
    },
}


class TestTreePoolConstruct(unittest.TestCase):
    def test_valid_init(self):
        pool = TreePool(copy.deepcopy(R_2NODE))
        self.assertIn(0, pool.impl._rank_type)
        self.assertIn(1, pool.impl._rank_type)

    def test_from_json_string(self):
        pool = TreePool(json.dumps(R_2NODE))
        self.assertEqual(len(pool.impl._rank_type), 2)

    def test_missing_scheduling_raises(self):
        R = copy.deepcopy(R_2NODE)
        del R["scheduling"]
        with self.assertRaises(ValueError):
            TreePool(R)

    def test_missing_children_raises(self):
        R = copy.deepcopy(R_2NODE)
        R["scheduling"] = {"writer": "TreePool"}
        with self.assertRaises(ValueError):
            TreePool(R)

    def test_empty_children_raises(self):
        R = copy.deepcopy(R_2NODE)
        R["scheduling"]["children"] = []
        with self.assertRaises(ValueError):
            TreePool(R)

    def test_rank_count_mismatch_raises(self):
        R = copy.deepcopy(R_2NODE)
        R["scheduling"]["children"].append(
            {
                "ranks": "2",
                "topo": {"socket": [{"numa": [{"cores": "0-3", "gpus": "0"}]}]},
            }
        )
        with self.assertRaises(ValueError):
            TreePool(R)

    def test_children_rank_keys_remapped(self):
        """Children ranks are re-mapped to match pool-rank space."""
        R = copy.deepcopy(R_2NODE)
        R["scheduling"]["children"] = [
            {"ranks": "10", "topo": {"socket": _SOCKETS_2}},
            {"ranks": "11", "topo": {"socket": _SOCKETS_2}},
        ]
        pool = TreePool(R)
        self.assertIn(0, pool.impl._rank_type)
        self.assertIn(1, pool.impl._rank_type)
        self.assertNotIn(10, pool.impl._rank_type)

    def test_idset_range_key_equivalent_to_per_rank(self):
        """A single IDset range key covers all ranks identically to per-rank keys."""
        # R_2NODE already uses a range key "0-1"
        pool_range = TreePool(copy.deepcopy(R_2NODE))
        # Build per-rank version with separate entries
        R_perrank = copy.deepcopy(R_2NODE)
        R_perrank["scheduling"]["children"] = [
            {"ranks": "0", "topo": {"socket": _SOCKETS_2}},
            {"ranks": "1", "topo": {"socket": _SOCKETS_2}},
        ]
        pool_perrank = TreePool(R_perrank)
        self.assertEqual(pool_range.impl._type_levels, pool_perrank.impl._type_levels)

    def test_idset_range_key_with_remapping(self):
        """IDset range key with non-zero original ranks is correctly re-mapped."""
        R = copy.deepcopy(R_2NODE)
        R["scheduling"]["children"] = [
            {"ranks": "10-11", "topo": {"socket": _SOCKETS_2}}
        ]
        pool = TreePool(R)
        self.assertIn(0, pool.impl._rank_type)
        self.assertIn(1, pool.impl._rank_type)
        self.assertNotIn(10, pool.impl._rank_type)
        self.assertNotIn(11, pool.impl._rank_type)
        children = pool.impl.scheduling["children"]
        children_ranks = frozenset(
            r for e in children for r in IDset(e.get("ranks", ""))
        )
        self.assertEqual(children_ranks, frozenset([0, 1]))

    def test_type_dedup_homogeneous(self):
        """Identical topology on all ranks → one shared type object."""
        pool = TreePool(copy.deepcopy(R_2NODE))
        # Both ranks are homogeneous; deduplication must yield one type.
        self.assertEqual(len(pool.impl._type_levels), 1)
        self.assertEqual(pool.impl._rank_type[0], pool.impl._rank_type[1])

    def test_type_dedup_heterogeneous(self):
        """Different topologies on two ranks → two distinct type objects."""
        R = copy.deepcopy(R_2NODE)
        R["scheduling"]["children"] = [
            {"ranks": "0", "topo": {"socket": _SOCKETS_2}},
            {
                "ranks": "1",
                "topo": {"socket": [{"numa": [{"cores": "0-7", "gpus": "0"}]}]},
            },
        ]
        pool = TreePool(R)
        self.assertEqual(len(pool.impl._type_levels), 2)
        self.assertNotEqual(pool.impl._rank_type[0], pool.impl._rank_type[1])


class TestTreePoolAlloc(unittest.TestCase):
    def setUp(self):
        self.pool = TreePool(copy.deepcopy(R_2NODE))

    def _assert_affinity(self, pool, result):
        """Assert each rank's allocated GPUs and cores share an affinity group."""
        for rank, rinfo in result.impl._ranks.items():
            alloc_cores = rinfo["cores"]
            alloc_gpus = rinfo["gpus"]
            if not alloc_gpus:
                continue
            levels = pool.impl._rank_levels(rank)
            self.assertTrue(
                any(
                    alloc_cores <= g_cores and alloc_gpus <= g_gpus
                    for level_groups in levels
                    for g_cores, g_gpus in level_groups
                ),
                f"rank {rank}: cores {alloc_cores} and gpus {alloc_gpus} "
                f"violate affinity",
            )

    def test_single_gpu_slot_affinity(self):
        """1-core 1-GPU slot: core and GPU must be in the same socket."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=1, gpu_per_slot=1))
        self._assert_affinity(self.pool, result)

    def test_full_group_slot_affinity(self):
        """4-core 1-GPU slot: fills one socket, affinity holds."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(self.pool, result)
        rank_info = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rank_info["gpus"]), 1)

    def test_cpu_only_ignores_affinity(self):
        """CPU-only request succeeds without NUMA constraint."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=8, gpu_per_slot=0))
        self.assertEqual(len(result.impl._ranks), 1)
        rank_info = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rank_info["cores"]), 8)
        self.assertEqual(len(rank_info["gpus"]), 0)

    def test_cpu_only_packs_into_partial_group(self):
        """CPU-only slots prefer partially-used groups, leaving intact groups free."""
        pool = TreePool(copy.deepcopy(R_1NODE))
        pool.alloc(1, rr(nslots=1, slot_size=2, gpu_per_slot=0))
        result2 = pool.alloc(2, rr(nslots=1, slot_size=2, gpu_per_slot=0))
        cores2 = result2.impl._ranks[0]["cores"]
        self.assertTrue(
            cores2 <= frozenset(range(4)),
            f"expected cores in group 0 (0-3), got {sorted(cores2)}",
        )

    def test_cpu_only_span_group_falls_back_to_base(self):
        """CPU-only slot larger than any single group falls back to base worst-fit."""
        pool = TreePool(copy.deepcopy(R_1NODE))
        result = pool.alloc(1, rr(nslots=1, slot_size=6, gpu_per_slot=0))
        rank_info = result.impl._ranks[0]
        self.assertEqual(len(rank_info["cores"]), 6)
        self.assertEqual(len(rank_info["gpus"]), 0)

    def test_two_slots_fill_one_node(self):
        """Two (4-core, 1-GPU) slots consume both sockets on one node."""
        result = self.pool.alloc(1, rr(nslots=2, slot_size=4, gpu_per_slot=1))
        self.assertEqual(len(result.impl._ranks), 1)
        rank_info = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rank_info["cores"]), 8)
        self.assertEqual(len(rank_info["gpus"]), 2)

    def test_node_based_gpu_request(self):
        """Node-based (nnodes=1) GPU request uses affinity allocation."""
        result = self.pool.alloc(1, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(self.pool, result)
        self.assertEqual(len(result.impl._ranks), 1)

    def test_cross_socket_gpu_slot_uses_whole_node(self):
        """5-core+1-GPU slot exceeds any socket group but succeeds via whole-node fallback."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=5, gpu_per_slot=1))
        rank_info = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rank_info["cores"]), 5)
        self.assertEqual(len(rank_info["gpus"]), 1)

    def test_infeasible_slot_too_many_gpus(self):
        """1-core+3-GPU slot is infeasible (groups have at most 1 GPU)."""
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(nslots=1, slot_size=1, gpu_per_slot=3))

    def test_cpu_only_oversized_is_infeasible(self):
        """CPU-only job exceeding total capacity raises InfeasibleRequest."""
        with self.assertRaises(InfeasibleRequest):
            self.pool.alloc(1, rr(nslots=1, slot_size=64, gpu_per_slot=0))

    def test_children_trimmed_to_allocated_ranks(self):
        """Allocated R's children covers only the ranks used by the job."""
        result = self.pool.alloc(1, rr(nnodes=1, nslots=1, slot_size=1, gpu_per_slot=1))
        allocated_ranks = frozenset(result.impl._ranks.keys())
        children = result.impl.scheduling.get("children", [])
        children_ranks = frozenset(
            r for e in children for r in IDset(e.get("ranks", ""))
        )
        self.assertEqual(children_ranks, allocated_ranks)

    def test_full_allocation_children_complete(self):
        """Allocating all nodes preserves full children rank coverage."""
        result = self.pool.alloc(1, rr(nnodes=2, nslots=2, slot_size=1, gpu_per_slot=1))
        children = result.impl.scheduling.get("children", [])
        children_ranks = frozenset(
            r for e in children for r in IDset(e.get("ranks", ""))
        )
        self.assertEqual(children_ranks, frozenset([0, 1]))

    def test_sequential_single_gpu_allocations_affinity(self):
        """Two sequential single-slot GPU allocations are each affinity-local."""
        r1 = self.pool.alloc(1, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        r2 = self.pool.alloc(2, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(self.pool, r1)
        self._assert_affinity(self.pool, r2)
        for rank in set(r1.impl._ranks) & set(r2.impl._ranks):
            self.assertTrue(
                r1.impl._ranks[rank]["cores"].isdisjoint(r2.impl._ranks[rank]["cores"])
            )

    def test_insufficient_after_full_allocation(self):
        """Slot request fails with InsufficientResources when pool is exhausted."""
        self.pool.alloc(1, rr(nslots=2, slot_size=4, gpu_per_slot=1))
        self.pool.alloc(2, rr(nslots=2, slot_size=4, gpu_per_slot=1))
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(3, rr(nslots=1, slot_size=1, gpu_per_slot=1))

    def test_check_feasibility_infeasible(self):
        """check_feasibility raises InfeasibleRequest when GPUs exceed node capacity."""
        req = rr(nslots=1, slot_size=1, gpu_per_slot=3)
        with self.assertRaises(InfeasibleRequest):
            self.pool.check_feasibility(req)

    def test_check_feasibility_feasible(self):
        """check_feasibility passes for a request that fits within a socket."""
        req = rr(nslots=1, slot_size=4, gpu_per_slot=1)
        self.pool.check_feasibility(req)  # must not raise


class TestTreePoolSingleNode(unittest.TestCase):
    """Tests against a single-node pool to simplify rank reasoning."""

    def setUp(self):
        self.pool = TreePool(copy.deepcopy(R_1NODE))

    def _assert_affinity(self, result):
        for rank, rinfo in result.impl._ranks.items():
            alloc_cores = rinfo["cores"]
            alloc_gpus = rinfo["gpus"]
            if not alloc_gpus:
                continue
            levels = self.pool.impl._rank_levels(rank)
            self.assertTrue(
                any(
                    alloc_cores <= g_cores and alloc_gpus <= g_gpus
                    for level_groups in levels
                    for g_cores, g_gpus in level_groups
                ),
                f"rank {rank}: cores {alloc_cores} gpus {alloc_gpus} "
                f"not in one affinity group",
            )

    def test_group0_allocation(self):
        """First 4-core+1-GPU slot uses group 0 (cores 0-3, GPU 0)."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(result)
        rinfo = result.impl._ranks[0]
        gpus = rinfo["gpus"]
        cores = rinfo["cores"]
        if 0 in gpus:
            self.assertTrue(cores <= frozenset(range(4)))
        else:
            self.assertTrue(cores <= frozenset(range(4, 8)))

    def test_second_slot_uses_other_group(self):
        """After first group is used, second allocation uses the other group."""
        r1 = self.pool.alloc(1, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        r2 = self.pool.alloc(2, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(r1)
        self._assert_affinity(r2)
        cores1 = r1.impl._ranks[0]["cores"]
        cores2 = r2.impl._ranks[0]["cores"]
        self.assertTrue(cores1.isdisjoint(cores2), "second alloc overlaps first")

    def test_third_slot_insufficient(self):
        """Third slot request fails after both groups are consumed."""
        self.pool.alloc(1, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self.pool.alloc(2, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(3, rr(nslots=1, slot_size=1, gpu_per_slot=1))

    def test_cpu_packing_preserves_group_for_gpu_job(self):
        """CPU-only packing leaves an intact group available for a subsequent GPU job."""
        self.pool.alloc(1, rr(nslots=1, slot_size=2, gpu_per_slot=0))
        result = self.pool.alloc(2, rr(nslots=1, slot_size=4, gpu_per_slot=1))
        self._assert_affinity(result)
        rinfo = result.impl._ranks[0]
        self.assertTrue(
            rinfo["cores"] <= frozenset(range(4, 8)),
            f"GPU job should use intact group 1, got cores {sorted(rinfo['cores'])}",
        )

    def test_children_trimmed_single_node(self):
        """Single-node pool: children trimmed to rank 0."""
        result = self.pool.alloc(1, rr(nslots=1, slot_size=1, gpu_per_slot=1))
        children = result.impl.scheduling.get("children", [])
        children_ranks = frozenset(
            r for e in children for r in IDset(e.get("ranks", ""))
        )
        self.assertEqual(children_ranks, frozenset([0]))


class TestTreePoolExclusive(unittest.TestCase):
    """Tests for exclusive node allocation path through TreePool affinity logic."""

    def setUp(self):
        self.pool = TreePool(copy.deepcopy(R_2NODE))

    def test_exclusive_gpu_claims_full_node(self):
        """Exclusive GPU request: affinity validates the node, full complement claimed.
        nslots must reflect the requested slot count, not the blocked core count."""
        result = self.pool.alloc(
            1, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        self.assertEqual(len(result.impl._ranks), 1)
        rinfo = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rinfo["cores"]), 8, "exclusive must claim all 8 cores")
        self.assertEqual(len(rinfo["gpus"]), 2, "exclusive must claim all 2 GPUs")
        self.assertEqual(result.impl._nslots, 1, "nslots must not be inflated")

    def test_exclusive_cpu_claims_full_node(self):
        """Exclusive CPU-only request: full core AND GPU complement is claimed so
        the node is truly exclusive.  nslots must not be inflated."""
        result = self.pool.alloc(
            1, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=0, exclusive=True)
        )
        self.assertEqual(len(result.impl._ranks), 1)
        rinfo = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rinfo["cores"]), 8, "exclusive must claim all 8 cores")
        self.assertEqual(len(rinfo["gpus"]), 2, "exclusive must claim all 2 GPUs")
        self.assertEqual(result.impl._nslots, 1, "nslots must not be inflated")

    def test_sequential_exclusive_use_separate_nodes(self):
        """Two sequential exclusive allocations land on different nodes."""
        r1 = self.pool.alloc(
            1, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        r2 = self.pool.alloc(
            2, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        ranks1 = set(r1.impl._ranks)
        ranks2 = set(r2.impl._ranks)
        self.assertTrue(
            ranks1.isdisjoint(ranks2), "exclusive allocs must land on different nodes"
        )

    def test_exclusive_prevents_double_booking(self):
        """After exclusive alloc, second job cannot use the same node."""
        self.pool.alloc(
            1, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        # Only one node remains free; exhaust it with another exclusive alloc.
        self.pool.alloc(
            2, rr(nnodes=1, nslots=1, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        # Pool is now fully exclusive — third request must fail.
        with self.assertRaises(InsufficientResources):
            self.pool.alloc(
                3, rr(nnodes=1, nslots=1, slot_size=1, gpu_per_slot=1, exclusive=True)
            )

    def test_exclusive_coarser_level_slot(self):
        """Exclusive slot_size=6 exceeds any socket group (4 cores): validated at
        the whole-node (coarser) level, which only fires outside the lvl_idx==0
        fast-path.  Full 8-core complement must still be claimed."""
        result = self.pool.alloc(
            1, rr(nnodes=1, nslots=1, slot_size=6, gpu_per_slot=0, exclusive=True)
        )
        self.assertEqual(len(result.impl._ranks), 1)
        rinfo = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rinfo["cores"]), 8, "exclusive must claim all 8 cores")

    def test_exclusive_multi_slot_spans_groups(self):
        """Exclusive alloc of 2 slots/node (one per socket group) validates both
        finest-level groups then claims the full node complement.
        nslots must be 2, not inflated by the extra blocked cores."""
        result = self.pool.alloc(
            1, rr(nnodes=1, nslots=2, slot_size=4, gpu_per_slot=1, exclusive=True)
        )
        self.assertEqual(len(result.impl._ranks), 1)
        rinfo = list(result.impl._ranks.values())[0]
        self.assertEqual(len(rinfo["cores"]), 8, "exclusive must claim all 8 cores")
        self.assertEqual(len(rinfo["gpus"]), 2, "exclusive must claim all 2 GPUs")
        self.assertEqual(result.impl._nslots, 2, "nslots must not be inflated")


if __name__ == "__main__":
    unittest.main(testRunner=TAPTestRunner())
