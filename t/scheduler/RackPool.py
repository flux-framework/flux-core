###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""RackPool: Rv1Pool extension demonstrating rack-local allocation hooks.

Rack topology is read from the ``scheduling.racks`` key of R.  The pool
is selected automatically when the system R carries a ``file://`` writer
URI pointing to this file, or explicitly via sched-simple's
``pool-class=`` argument::

    flux module load sched-simple pool-class=file:///path/to/RackPool.py

The ``R.scheduling`` key format expected by this pool::

    {
      "scheduling": {
        "writer": "file:///path/to/RackPool.py",
        "racks": [
          {"id": 0, "ranks": "0-3"},
          {"id": 1, "ranks": "4-7"}
        ]
      }
    }

Allocated R objects carry a trimmed ``scheduling`` key containing only the
ranks that were allocated, so that a nested sub-instance scheduler sees only
its own portion of the rack topology.  On sub-instance startup the resource
module re-ranks the pool from 0 but treats the ``scheduling`` key as opaque,
so :class:`_RackPoolV1` re-maps the ``scheduling`` ranks to match by zipping
the two sorted rank sequences.

This module demonstrates three :class:`~flux.resource.Rv1Pool.Rv1Pool`
extension hooks:

- :meth:`~flux.resource.Rv1Pool.Rv1Pool._select_resources` — rack-local
  candidate selection.
- :meth:`~flux.resource.Rv1Pool.Rv1Pool._check_feasibility` — permanent
  denial when the request cannot fit in any single rack.
- :meth:`~flux.resource.ResourcePool.ResourcePool` wrapping pattern —
  version-dispatching outer class compatible with
  :attr:`~flux.scheduler.Scheduler.pool_class`.
"""

import json
from collections.abc import Mapping

from flux.idset import IDset
from flux.resource import InfeasibleRequest, InsufficientResources
from flux.resource.ResourcePool import ResourcePool
from flux.resource.Rv1Pool import Rv1Pool


def _build_rack_map(scheduling):
    """Return a dict mapping rank -> rack_id from R.scheduling.racks."""
    rack_map = {}
    for rack in (scheduling or {}).get("racks", []):
        rack_id = rack["id"]
        for rank in IDset(rack["ranks"]):
            rack_map[rank] = rack_id
    return rack_map


def _ranks_to_idset_str(ranks):
    """Convert a sorted list of rank integers to a compact IDset string."""
    return str(IDset(",".join(str(r) for r in ranks)))


def _filter_racks(racks, rank_set):
    """Return racks with entries restricted to ranks in rank_set."""
    result = []
    for rack in racks:
        kept = sorted(r for r in IDset(rack["ranks"]) if r in rank_set)
        if kept:
            result.append({"id": rack["id"], "ranks": _ranks_to_idset_str(kept)})
    return result


def _rerank_racks(racks, rank_remap):
    """Return racks with rank numbers translated through rank_remap."""
    result = []
    for rack in racks:
        new_ranks = sorted(
            rank_remap[r] for r in IDset(rack["ranks"]) if r in rank_remap
        )
        if new_ranks:
            result.append({"id": rack["id"], "ranks": _ranks_to_idset_str(new_ranks)})
    return result


class _RackPoolV1(Rv1Pool):
    """Rv1Pool subclass that constrains allocation to a single rack."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.scheduling is None:
            raise ValueError("RackPool requires R.scheduling")
        scheduling = dict(self.scheduling)
        rack_map = _build_rack_map(scheduling)
        if not rack_map:
            raise ValueError("RackPool requires R.scheduling with rack topology")

        # Re-map scheduling ranks to pool ranks.  The sub-instance resource
        # module always re-ranks allocated nodes from 0, so the n-th rank in
        # scheduling (sorted ascending) corresponds to the n-th pool rank.
        # Apply the sorted-zip unconditionally; when ranks already match it
        # produces the identity mapping and is a no-op.
        if rack_map:
            sorted_orig = sorted(rack_map)
            if len(sorted_orig) != len(self._ranks):
                raise ValueError(
                    f"sched-rack: rank count mismatch: "
                    f"{len(sorted_orig)} in scheduling vs {len(self._ranks)} in pool"
                )
            rank_remap = dict(zip(sorted_orig, range(len(self._ranks))))
            rack_map = {rank_remap[o]: rid for o, rid in rack_map.items()}
            scheduling["racks"] = _rerank_racks(scheduling.get("racks", []), rank_remap)

        self.scheduling = scheduling
        self._rack_map = rack_map

    def alloc(self, jobid, request):
        result = super().alloc(jobid, request)
        # Trim scheduling.racks to only the allocated ranks so that a
        # sub-instance scheduler inherits only its portion of the topology.
        if result.scheduling:
            result.scheduling = dict(result.scheduling)
            result.scheduling["racks"] = _filter_racks(
                result.scheduling.get("racks", []), set(result._ranks)
            )
        return result

    def _select_resources(self, candidates, request):
        """Select candidates from a single rack.

        If ``attributes.system.rack_exclusive`` is true in the jobspec, the
        entire rack is allocated — all available nodes in the chosen rack are
        reserved for the job even if fewer were requested.  Otherwise, tries
        each rack in order of most available candidates and delegates to the
        base greedy loop.  Falls back to base behaviour if no rack topology
        is present.
        """
        if not self._rack_map:
            return super()._select_resources(candidates, request)

        rack_exclusive = (
            request.jobspec.get("attributes", {})
            .get("system", {})
            .get("rack_exclusive", False)
        )

        racks = {}
        for entry in candidates:
            rack_id = self._rack_map.get(entry[0])
            if rack_id is not None:
                racks.setdefault(rack_id, []).append(entry)

        if rack_exclusive:
            # Whole-rack exclusive: allocate every available node in the
            # chosen rack.  Requires at least request.nnodes candidates so
            # the base request is satisfiable within the rack.
            nnodes = request.nnodes if request.nnodes > 0 else 1
            for _count, rack_candidates in sorted(
                ((len(v), v) for v in racks.values()), reverse=True
            ):
                if len(rack_candidates) < nnodes:
                    continue
                selected = [
                    (rank, info, frozenset(fc), frozenset(fg))
                    for rank, info, fc, fg in rack_candidates
                ]
                actual_nslots = sum(
                    len(ac) // request.slot_size for _, _, ac, _ in selected
                )
                return selected, actual_nslots
            raise InsufficientResources(
                "rack_exclusive: no rack has sufficient available nodes"
            )

        # Normal rack-local: try racks in descending order of available candidates
        for _count, rack_candidates in sorted(
            ((len(v), v) for v in racks.values()), reverse=True
        ):
            try:
                return super()._select_resources(rack_candidates, request)
            except InsufficientResources:
                continue

        raise InsufficientResources("no single rack has sufficient resources")

    def _check_feasibility(self, request):
        """Extend base feasibility check with rack-size constraint."""
        super()._check_feasibility(request)

        if not self._rack_map or request.nnodes == 0:
            return

        rack_node_counts = {}
        for rank in self._ranks:
            rack_id = self._rack_map.get(rank)
            if rack_id is not None:
                rack_node_counts[rack_id] = rack_node_counts.get(rack_id, 0) + 1

        if not rack_node_counts:
            return
        max_rack_nodes = max(rack_node_counts.values())
        if max_rack_nodes < request.nnodes:
            raise InfeasibleRequest(
                f"rack-local request for {request.nnodes} node(s) cannot be "
                f"satisfied: largest rack has {max_rack_nodes} node(s)"
            )


class RackPool(ResourcePool):
    """Version-dispatching rack pool.

    Wraps a version-specific rack implementation — currently :class:`_RackPoolV1`
    for Rv1 resources.  Inheriting from
    :class:`~flux.resource.ResourcePool.ResourcePool` means that
    :attr:`~flux.scheduler.Scheduler.pool_class` can be set to this class
    directly: :meth:`~flux.scheduler.Scheduler._make_pool` gets a
    :class:`~flux.resource.ResourcePool.ResourcePool`-compatible object back
    without any extra wrapping, and the built-in version dispatch is preserved
    here rather than in the scheduler base class.

    To support a future R version, add a version-specific implementation
    class and extend ``_impl_map``.
    """

    _impl_map = {1: _RackPoolV1}

    def __init__(self, R, log=None):
        if isinstance(R, str):
            R = json.loads(R)
        version = R.get("version", 1) if isinstance(R, Mapping) else 1
        impl_class = self._impl_map.get(version)
        if impl_class is None:
            raise ValueError(f"R version {version} not supported by RackPool")
        super().__init__(impl_class(R, log=log))
