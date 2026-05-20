###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""TreePool: Rv1Pool extension for sub-node affinity-aware GPU+core allocation.

Reads ``scheduling.children`` from the Rv1 R object to build a sub-node
topology tree.  GPU+core slots are allocated from the finest topology level
(e.g. NUMA node) that can satisfy the request, so GPU and core share tight
locality.  Falls back to coarser levels (socket, whole node) when necessary.
CPU-only slots use best-fit within the finest fitting level to preserve
intact groups for future GPU jobs.

``children`` is a list of objects.  Each object has a ``ranks`` key
(RFC 22 IDset string identifying which broker ranks share this layout)
and a ``topo`` key holding the node topology object.  ``cores`` and
``gpus`` within leaf nodes are RFC 22 IDset strings using node-local IDs.

Node with no named topology levels (resources appear directly in *topo*)::

    {"ranks": "0-15", "topo": {"cores": "0-7", "memory": 128}}

Two-socket node (``socket`` array groups cores by physical package)::

    {
      "ranks": "0-15",
      "topo": {
        "socket": [
          {"numa": [
            {"cores": "0-14",  "gpus": "0"},
            {"cores": "15-29", "gpus": "1"}
          ]},
          {"numa": [
            {"cores": "30-44", "gpus": "2"},
            {"cores": "45-59", "gpus": "3"}
          ]}
        ]
      }
    }

Levels that carry no useful grouping information MAY be omitted.
Sites MAY introduce additional locality names (e.g. ``apu``) to create
aliases or extra containment layers; all named levels are available for
container-exclusive allocation.

Topology deduplication
    Large clusters often have only 2-3 distinct node types.  The scheduler
    stores one topology structure per unique node type rather than one per
    rank, so memory is O(types × resources_per_type) rather than
    O(nodes × resources_per_type).

Node-exclusive fast path
    When a job requests node-exclusive allocation (``exclusive: true`` at
    the node level, set by the frobnicator as a site policy), every
    candidate node has its full complement of cores and GPUs free.  At
    the finest sub-node topology level, groups are guaranteed disjoint, so
    the per-group core intersection is skipped.  After the affinity helpers
    confirm that the node can satisfy the slot count, the full node
    complement is claimed so the node is correctly marked as exclusive.
"""

import json
from collections.abc import Mapping

from flux.idset import IDset
from flux.resource import InsufficientResources
from flux.resource.ResourceCount import ResourceCount
from flux.resource.ResourcePool import ResourcePool
from flux.resource.ResourcePoolImplementation import (
    InfeasibleRequest,
    ResourcePoolImplementation,
)
from flux.resource.Rv1Pool import ResourceRequest, Rv1Pool

# Guard against pathological (or malicious) jobspec resource nesting.  Real
# jobspecs nest only a handful of levels (node/slot/core/gpu); this bound is
# far above any legitimate request but well below Python's recursion limit.
MAX_RESOURCE_DEPTH = 100


def _all_resources(node):
    """Recursively collect all cores and gpus in a topo node."""
    cores = frozenset(IDset(node["cores"])) if "cores" in node else frozenset()
    gpus = frozenset(IDset(node["gpus"])) if "gpus" in node else frozenset()
    for val in node.values():
        if isinstance(val, list):
            for child in val:
                if isinstance(child, dict):
                    cc, cg = _all_resources(child)
                    cores |= cc
                    gpus |= cg
    return cores, gpus


def _groups_for_container(topo, name):
    """Return list of (cores_frozenset, gpus_frozenset) for each instance of
    the named container in topo.  Returns [] if the name is not found.

    Collects all instances at the same depth so that, e.g., searching for
    "numa" in a two-socket node returns all eight NUMA domains rather than
    stopping after the first socket's four.
    """
    if not topo:
        return []
    if name in topo and isinstance(topo[name], list):
        return [_all_resources(item) for item in topo[name] if isinstance(item, dict)]
    results = []
    for val in topo.values():
        if isinstance(val, list):
            for item in val:
                if isinstance(item, dict):
                    results.extend(_groups_for_container(item, name))
    return results


class TreeResourceRequest(ResourceRequest):
    """ResourceRequest subclass for TreePool supporting RFC 14 canonical jobspec.

    Extends the base RFC 25 V1 parser with:

    - Container-exclusive allocation: an exclusive leaf vertex whose type is
      not node/slot/core/gpu is treated as a topo locality name (e.g.
      ``socket{x}``, ``numa{x}``).  ``container_level`` is set to that type
      and ``slot_size`` is 0; the actual per-container resource counts are
      resolved from the pool topology at scheduling time.

    - Node-exclusive without explicit cores: ``slot=N/node{x}`` is valid even
      without a ``core`` child.  ``slot_size`` is 0; the scheduler claims the
      full resource complement of the chosen node.  When a ``core`` child is
      present its count acts as a minimum capability filter.
    """

    __slots__ = ("container_level",)

    def __init__(self, *args, container_level=None, **kwargs):
        super().__init__(*args, **kwargs)
        self.container_level = container_level

    @classmethod
    def from_jobspec(cls, jobspec):
        """Parse a jobspec and return a :class:`TreeResourceRequest`."""
        resources = jobspec.get("resources", [])
        if not resources:
            raise ValueError("jobspec has no resources")

        state = {
            "nnodes": None,
            "nslots": None,
            "slot_size": None,
            "gpu_per_slot": 0,
            "exclusive": False,
            "nodefactor": 1,
            "slot_above_node": False,
            "container_level": None,
        }

        def walk(res_list, nodefactor, depth=0):
            if depth > MAX_RESOURCE_DEPTH:
                raise ValueError(
                    f"jobspec resource nesting exceeds maximum depth "
                    f"{MAX_RESOURCE_DEPTH}"
                )
            for vertex in res_list:
                rtype = vertex.get("type", "")
                count = ResourceCount.from_count_spec(vertex.get("count", 1))
                children = vertex.get("with", [])
                if rtype == "node":
                    state["nnodes"] = count
                    state["nodefactor"] = nodefactor
                    if state["nslots"] is not None:
                        state["slot_above_node"] = True
                    if vertex.get("exclusive", False):
                        state["exclusive"] = True
                    if children:
                        walk(children, nodefactor, depth + 1)
                else:
                    new_nf = nodefactor * count.min
                    if rtype == "slot":
                        state["nslots"] = count
                        if vertex.get("exclusive", False):
                            state["exclusive"] = True
                    elif rtype == "core":
                        state["slot_size"] = count.min
                    elif rtype == "gpu":
                        state["gpu_per_slot"] = count.min
                    elif vertex.get("exclusive", False) and not children:
                        # Exclusive topo-container vertex (e.g. socket{x},
                        # numa{x}): resource counts resolved from pool topo.
                        state["container_level"] = rtype
                    if children:
                        walk(children, new_nf, depth + 1)

        walk(resources, 1)

        system = jobspec.get("attributes", {}).get("system")
        if jobspec.get("version") == 1:
            if system is None:
                raise ValueError("getting duration: Object item not found: system")
            if system.get("duration") is None:
                raise ValueError("getting duration: Object item not found: duration")

        if state["nslots"] is None:
            raise ValueError("Unable to determine slot count")
        if (
            state["slot_size"] is None
            and state["container_level"] is None
            and not state["exclusive"]
        ):
            raise ValueError("Unable to determine slot size")

        nf = state["nodefactor"]
        sc = state["nslots"]

        if state["nnodes"] is not None:
            node_count = state["nnodes"].scaled(nf)
            if state["slot_above_node"] and nf > 1 and sc.min % nf == 0:
                # slot is an ancestor of node: the slot count was folded into
                # the nodefactor, so divide it back out to recover the per-node
                # slot count.
                slot_count = ResourceCount(
                    sc.min // nf,
                    None if sc.max is None else sc.max // nf,
                )
            else:
                # Either a normal node-above-slot layout, or a slot-above-node
                # layout whose slot count does not divide evenly into the
                # nodefactor (only reachable with an intermediate multiplier
                # vertex between slot and node).  The unfold is ambiguous in
                # that case, so pass the slot count through unmodified rather
                # than guess; nslots then reflects the literal jobspec counts.
                slot_count = sc
        else:
            node_count = None
            slot_count = sc

        exclusive = state["exclusive"]
        container_level = state["container_level"]
        # Exclusive slot-only fold: each slot gets one exclusive node.
        # Skip for container-exclusive: slots pack across nodes by container.
        if exclusive and node_count is None and container_level is None:
            node_count = slot_count
            slot_count = ResourceCount(1, 1)
        attrs = system or {}
        duration = attrs.get("duration") or 0.0
        constraint = attrs.get("constraints") or None
        return cls(
            node_count,
            slot_count,
            state["slot_size"] if state["slot_size"] is not None else 0,
            state["gpu_per_slot"],
            float(duration),
            constraint,
            exclusive,
            jobspec,
            container_level=container_level,
        )


def _extract_levels(node):
    """Recursively extract affinity groups at each topology level.

    Returns ``(levels, total_cores, total_gpus)`` where ``levels[0]`` is the
    finest granularity and ``levels[-1]`` is a synthetic whole-node group
    (union of all children).  Each entry in *levels* is a list of
    ``(cores_frozenset, gpus_frozenset)`` groups.  Adjacent identical levels
    (e.g. a node with a single child whose union equals the parent) are
    deduplicated.
    """
    if "cores" in node:
        cores = frozenset(IDset(node["cores"]))
        gpus = frozenset(IDset(node["gpus"])) if "gpus" in node else frozenset()
        return [[(cores, gpus)]], cores, gpus

    merged = []
    total_cores = frozenset()
    total_gpus = frozenset()

    for val in node.values():
        if not isinstance(val, list):
            continue
        for child in val:
            if not isinstance(child, dict):
                continue
            child_levels, cc, cg = _extract_levels(child)
            total_cores |= cc
            total_gpus |= cg
            for i, groups in enumerate(child_levels):
                if i >= len(merged):
                    merged.append([])
                merged[i].extend(groups)

    if total_cores:
        this_group = (total_cores, total_gpus)
        if not (merged and set(merged[-1]) == {this_group}):
            merged.append([this_group])

    return merged, total_cores, total_gpus


def _parse_children(children):
    """Parse ``scheduling.children`` → ``{rank: (levels, topo)}``.

    *children* is a list of objects each with a ``ranks`` key (IDset string)
    and a ``topo`` key holding the node topology object.
    """
    groups_by_rank = {}
    for entry in children or []:
        rank_str = entry.get("ranks", "")
        topo = entry.get("topo")
        if not rank_str or not topo:
            continue
        levels, _, _ = _extract_levels(topo)
        if levels:
            for rank in IDset(rank_str):
                groups_by_rank[rank] = (levels, topo)
    return groups_by_rank


def _dedup_topology(groups_by_rank):
    """Deduplicate topology so ranks with identical structure share one object.

    Large homogeneous clusters have 2-3 distinct node types; sharing the
    level structure avoids O(nodes) copies of the same frozensets.

    Returns ``(type_levels, type_topos, rank_type)`` where:

    - ``type_levels``: list of unique level structures (one per node type)
    - ``type_topos``: list of raw topo dicts (one per node type)
    - ``rank_type``: ``{rank: type_index}``
    """
    canonical_to_idx = {}
    type_levels = []
    type_topos = []
    rank_type = {}
    for rank, (levels, topo) in groups_by_rank.items():
        key = tuple(tuple(level) for level in levels)
        if key not in canonical_to_idx:
            canonical_to_idx[key] = len(type_levels)
            type_levels.append(levels)
            type_topos.append(topo)
        rank_type[rank] = canonical_to_idx[key]
    return type_levels, type_topos, rank_type


class _TreePoolV1(Rv1Pool):
    """Rv1Pool subclass providing sub-node affinity-aware GPU+core allocation."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        if self.scheduling is None:
            raise ValueError("TreePool requires R.scheduling")
        scheduling = dict(self.scheduling)

        children = scheduling.get("children")
        if not children:
            raise ValueError("TreePool requires scheduling.children")

        raw_groups = _parse_children(children)
        if not raw_groups:
            raise ValueError("TreePool: children contains no groups")

        # Children rank keys may be the enclosing instance's ranks; map them to
        # pool-rank space via sorted-zip.
        sorted_orig = sorted(raw_groups)
        if len(sorted_orig) != len(self._ranks):
            raise ValueError(
                f"TreePool: rank count mismatch: "
                f"{len(sorted_orig)} in children vs {len(self._ranks)} in pool"
            )
        rank_remap = dict(zip(sorted_orig, range(len(self._ranks))))
        # The remap targets pool-rank space (range(N)); _rank_type is keyed in
        # that space so _rank_topo/_rank_levels can index it directly.  This
        # relies on the pool's own ranks being exactly that zero-origin range.
        # The resource module guarantees this today: R from resource.acquire is
        # always 0-origin contiguous (see inventory.c -- configured/discovered R
        # is 0-origin, and sub-instance R is renumbered from zero on load).  The
        # check below is defense-in-depth: if that cross-component contract ever
        # changes, fail loudly here rather than silently mapping every rank to
        # node type 0.
        if set(self._ranks) != set(rank_remap.values()):
            raise ValueError(
                f"TreePool: pool ranks {sorted(self._ranks)} are not the "
                f"zero-origin range required for topology remapping"
            )
        remapped = {
            rank_remap[o]: (lvls, topo) for o, (lvls, topo) in raw_groups.items()
        }
        # Deduplicate: homogeneous clusters share the level structure
        # across all ranks of the same node type.
        self._type_levels, self._type_topo, self._rank_type = _dedup_topology(remapped)
        # Rewrite children with remapped ranks for sub-instances.
        new_children = []
        for entry in children:
            rank_str = entry.get("ranks", "")
            new_ranks = {rank_remap[r] for r in IDset(rank_str) if r in rank_remap}
            if new_ranks:
                new_entry = dict(entry)
                new_entry["ranks"] = str(IDset(new_ranks))
                new_children.append(new_entry)
        scheduling["children"] = new_children

        self.scheduling = scheduling

    name = "TreePool"

    def parse_resource_request(self, jobspec):
        """Parse jobspec using the TreePool-specific request parser."""
        return TreeResourceRequest.from_jobspec(jobspec)

    def _init_from_state(self, source):
        # type_levels and type_topo are read-only after __init__; share refs.
        self._type_levels = source._type_levels
        self._type_topo = source._type_topo
        self._rank_type = source._rank_type

    def alloc(self, jobid, request):
        result = super().alloc(jobid, request)
        if result.scheduling:
            result.scheduling = dict(result.scheduling)
            allocated_set = set(result._ranks)
            # Trim children ranks to only the allocated nodes so the sub-instance
            # rank count matches its pool size.
            if "children" in result.scheduling:
                new_children = []
                for entry in result.scheduling["children"]:
                    kept = frozenset(IDset(entry.get("ranks", ""))) & allocated_set
                    if kept:
                        new_entry = dict(entry)
                        new_entry["ranks"] = str(IDset(kept))
                        new_children.append(new_entry)
                result.scheduling["children"] = new_children
        return result

    def _rank_levels(self, rank):
        """Return the sub-node level list for *rank*; empty list if no topology."""
        if not self._type_levels:
            return []
        return self._type_levels[self._rank_type[rank]]

    def _rank_topo(self, rank):
        """Return the raw topo dict for *rank*; empty dict if no topology."""
        if not self._type_topo:
            return {}
        return self._type_topo[self._rank_type[rank]]

    def _check_feasibility(self, request):
        """Override to handle container-exclusive requests before base checks."""
        container_level = getattr(request, "container_level", None)
        if container_level is not None:
            nslots = request.nslots
            nnodes = request.nnodes
            # Honor any RFC 31 constraint: only ranks the request is allowed to
            # land on count toward availability, matching the constraint
            # filtering the base alloc() applies to candidates.
            constraint = request.constraint
            if constraint is not None and isinstance(constraint, str):
                constraint = json.loads(constraint)
            per_rank = [
                len(_groups_for_container(self._rank_topo(rank), container_level))
                for rank, info in self._ranks.items()
                if constraint is None
                or self._matches_constraint(rank, info, constraint)
            ]
            if nnodes > 0:
                # The request spreads nslots containers across nnodes distinct
                # nodes (nslots // nnodes each), mirroring the allocation in
                # _container_exclusive_select.  Count nodes that can supply a
                # full per-node share rather than the raw container total.
                containers_per_node = nslots // nnodes
                eligible = sum(1 for n in per_rank if n >= containers_per_node)
                if eligible < nnodes:
                    raise InfeasibleRequest(
                        f"unsatisfiable request: need {nnodes} node(s) with "
                        f"{containers_per_node} exclusive '{container_level}' "
                        f"container(s) each, only {eligible} eligible"
                    )
                return
            total_containers = sum(per_rank)
            if total_containers < nslots:
                raise InfeasibleRequest(
                    f"unsatisfiable request: need {nslots} exclusive "
                    f"'{container_level}' container(s), only "
                    f"{total_containers} available"
                )
            return
        super()._check_feasibility(request)

    def _container_take_from_rank(
        self, rank, free_cores, free_gpus, container_level, max_take
    ):
        """Take up to *max_take* exclusive containers from one rank.

        Returns ``(alloc_cores, alloc_gpus, ntaken)``.  *max_take* of None
        means take every free container on the rank.
        """
        groups = _groups_for_container(self._rank_topo(rank), container_level)
        free_groups = [
            (gc, gg) for gc, gg in groups if gc <= free_cores and gg <= free_gpus
        ]
        if max_take is not None:
            free_groups = free_groups[:max_take]
        if not free_groups:
            return frozenset(), frozenset(), 0
        alloc_cores = frozenset().union(*(gc for gc, _ in free_groups))
        alloc_gpus = frozenset().union(*(gg for _, gg in free_groups))
        return alloc_cores, alloc_gpus, len(free_groups)

    def _container_exclusive_select(self, candidates, request):
        """Select container-exclusive allocations (e.g. socket{{x}}, numa{{x}}).

        With an explicit node count the request is spread across that many
        distinct nodes, taking ``nslots // nnodes`` containers from each, so a
        ``node{{N}}`` request is not collapsed onto a single node that happens
        to supply enough containers.  Without a node count, containers are
        packed across candidates until the slot count is met.
        """
        container_level = getattr(request, "container_level", None)
        nnodes = request.nnodes
        nslots = request.nslots
        selected = []

        if nnodes > 0:
            containers_per_node = nslots // nnodes
            nnodes_target = request.nnodes_max
            for rank, info, free_cores, free_gpus in candidates:
                if nnodes_target is not None and len(selected) >= nnodes_target:
                    break
                alloc_cores, alloc_gpus, n = self._container_take_from_rank(
                    rank, free_cores, free_gpus, container_level, containers_per_node
                )
                if n < containers_per_node:
                    continue
                selected.append((rank, alloc_cores, alloc_gpus))
            if len(selected) < nnodes:
                self._check_feasibility(request)
                raise InsufficientResources("insufficient resources")
            return selected, containers_per_node * len(selected)

        nslots_target = request.nslots_max
        allocated_slots = 0
        for rank, info, free_cores, free_gpus in candidates:
            if nslots_target is not None and allocated_slots >= nslots_target:
                break
            max_take = (
                nslots_target - allocated_slots if nslots_target is not None else None
            )
            alloc_cores, alloc_gpus, n = self._container_take_from_rank(
                rank, free_cores, free_gpus, container_level, max_take
            )
            if n > 0:
                selected.append((rank, alloc_cores, alloc_gpus))
                allocated_slots += n

        if allocated_slots < nslots:
            self._check_feasibility(request)
            raise InsufficientResources("insufficient resources")
        return selected, allocated_slots

    def _affinity_alloc_from_rank(
        self, rank, free_cores, free_gpus, request, max_slots=None, exclusive=False
    ):
        """Allocate affinity-local GPU slots from one rank.

        Iterates levels finest→coarsest.  Within each level, allocates from
        groups in order until *max_slots* is reached or the rank is exhausted.

        When *exclusive* is True (node-exclusive allocation) all cores and GPUs
        on the node are free.  At the finest level (lvl_idx==0) groups are
        disjoint, so the intersection with remaining resources is skipped.
        Coarser levels still use the intersection because prior fine-level
        allocations may have removed cores that appear in a coarser group.
        """
        slot_size = request.slot_size
        gpu_per_slot = request.gpu_per_slot
        if slot_size == 0:
            # node{x} without core constraint: caller's exclusive override
            # claims all resources; signal one slot satisfied.
            return frozenset(), frozenset(), 1
        remaining_cores = set(free_cores)
        remaining_gpus = set(free_gpus)
        rank_cores = set()
        rank_gpus = set()
        nslots = 0

        for lvl_idx, level_groups in enumerate(self._rank_levels(rank)):
            for group_cores, group_gpus in level_groups:
                if exclusive and lvl_idx == 0:
                    # Finest level: all group resources are free and groups are
                    # disjoint — use group sets directly without intersection.
                    avail_cores = set(group_cores)
                    avail_gpus = set(group_gpus)
                else:
                    avail_cores = remaining_cores & group_cores
                    avail_gpus = remaining_gpus & group_gpus
                while len(avail_cores) >= slot_size and len(avail_gpus) >= gpu_per_slot:
                    if max_slots is not None and nslots >= max_slots:
                        return frozenset(rank_cores), frozenset(rank_gpus), nslots
                    taken_cores = frozenset(sorted(avail_cores)[:slot_size])
                    taken_gpus = frozenset(sorted(avail_gpus)[:gpu_per_slot])
                    rank_cores |= taken_cores
                    rank_gpus |= taken_gpus
                    remaining_cores -= taken_cores
                    remaining_gpus -= taken_gpus
                    avail_cores -= taken_cores
                    avail_gpus -= taken_gpus
                    nslots += 1

        return frozenset(rank_cores), frozenset(rank_gpus), nslots

    def _cpu_alloc_from_rank(
        self, rank, free_cores, request, max_slots=None, exclusive=False
    ):
        """Allocate CPU-only slots from one rank.

        Finds the finest level where any group can accommodate *slot_size* cores,
        then uses best-fit (fewest-available-first) within that level to preserve
        intact groups for future GPU jobs.

        When *exclusive* is True the node's full core complement is available;
        group checks and scoring use the group sets directly without intersection.
        """
        slot_size = request.slot_size
        if slot_size == 0:
            return frozenset(), 1
        remaining = set(free_cores)
        rank_cores = set()
        nslots = 0

        levels = self._rank_levels(rank)
        chosen_groups = None
        use_excl = False
        for lvl_idx, level_groups in enumerate(levels):
            if exclusive and lvl_idx == 0:
                if any(len(gc) >= slot_size for gc, _gg in level_groups):
                    chosen_groups = level_groups
                    use_excl = True
                    break
            else:
                if any(len(remaining & gc) >= slot_size for gc, _gg in level_groups):
                    chosen_groups = level_groups
                    break

        if chosen_groups is None:
            return frozenset(), 0

        # Best-fit: fewest-available-first preserves intact groups for GPU jobs.
        if use_excl:
            scored = sorted(
                ((len(gc), gc) for gc, _gg in chosen_groups), key=lambda t: t[0]
            )
        else:
            scored = sorted(
                ((len(remaining & gc), gc) for gc, _gg in chosen_groups),
                key=lambda t: t[0],
            )
        for _avail, group_cores in scored:
            avail = set(group_cores) if use_excl else (remaining & group_cores)
            while len(avail) >= slot_size:
                if max_slots is not None and nslots >= max_slots:
                    return frozenset(rank_cores), nslots
                taken = frozenset(sorted(avail)[:slot_size])
                rank_cores |= taken
                remaining -= taken
                avail -= taken
                nslots += 1

        return frozenset(rank_cores), nslots

    def _sort_candidates(self, candidates):
        """Best-fit: ascending free cores so partially-used nodes are preferred."""
        return sorted(candidates, key=lambda x: (len(x[2]), len(x[3])))

    def _select_resources(self, candidates, request):
        """Apply sub-node affinity selection to *candidates*.

        Candidates arrive best-fit sorted (via ``_sort_candidates``).  Falls
        through to the base class only when no sub-node topology is present.
        Exclusive requests use the affinity fast-path in the helpers to
        validate node eligibility, then claim the full node.
        """
        if not self._type_levels:
            return super()._select_resources(candidates, request)
        if getattr(request, "container_level", None) is not None:
            return self._container_exclusive_select(candidates, request)
        if request.gpu_per_slot == 0:
            return self._cpu_select(candidates, request)
        return self._gpu_select(candidates, request)

    def _cpu_select(self, candidates, request):
        """CPU-only selection using finest-level best-fit packing."""
        exclusive = request.exclusive
        nnodes = request.nnodes
        nslots = request.nslots
        selected = []

        if nnodes > 0:
            slots_per_node = nslots // nnodes
            nnodes_target = request.nnodes_max
            for rank, info, free_cores, free_gpus in candidates:
                if nnodes_target is not None and len(selected) >= nnodes_target:
                    break
                alloc_cores, n = self._cpu_alloc_from_rank(
                    rank,
                    free_cores,
                    request,
                    max_slots=slots_per_node,
                    exclusive=exclusive,
                )
                if n < slots_per_node:
                    continue
                if exclusive:
                    alloc_cores = frozenset(free_cores)
                    alloc_gpus = frozenset(free_gpus)
                else:
                    alloc_gpus = frozenset()
                selected.append((rank, alloc_cores, alloc_gpus))
            if len(selected) < nnodes:
                self._check_feasibility(request)
                raise InsufficientResources("insufficient resources")
            actual_nslots = slots_per_node * len(selected)
        else:
            nslots_target = request.nslots_max
            allocated_slots = 0
            for rank, info, free_cores, free_gpus in candidates:
                if nslots_target is not None and allocated_slots >= nslots_target:
                    break
                max_s = (
                    nslots_target - allocated_slots
                    if nslots_target is not None
                    else None
                )
                alloc_cores, n = self._cpu_alloc_from_rank(
                    rank, free_cores, request, max_slots=max_s, exclusive=exclusive
                )
                if n > 0:
                    selected.append((rank, alloc_cores, frozenset()))
                    allocated_slots += n
            if allocated_slots < nslots:
                self._check_feasibility(request)
                raise InsufficientResources("insufficient resources")
            actual_nslots = allocated_slots

        return selected, actual_nslots

    def _gpu_select(self, candidates, request):
        """GPU slot selection with affinity-local core+GPU pairing."""
        exclusive = request.exclusive
        nnodes = request.nnodes
        nslots = request.nslots
        selected = []

        if nnodes > 0:
            slots_per_node = nslots // nnodes
            nnodes_target = request.nnodes_max
            for rank, info, free_cores, free_gpus in candidates:
                if nnodes_target is not None and len(selected) >= nnodes_target:
                    break
                alloc_cores, alloc_gpus, n = self._affinity_alloc_from_rank(
                    rank,
                    free_cores,
                    free_gpus,
                    request,
                    max_slots=slots_per_node,
                    exclusive=exclusive,
                )
                if n < slots_per_node:
                    continue
                if exclusive:
                    alloc_cores = frozenset(free_cores)
                    alloc_gpus = frozenset(free_gpus)
                selected.append((rank, alloc_cores, alloc_gpus))
            if len(selected) < nnodes:
                self._check_feasibility(request)
                raise InsufficientResources("insufficient resources")
            actual_nslots = slots_per_node * len(selected)
        else:
            nslots_target = request.nslots_max
            allocated_slots = 0
            for rank, info, free_cores, free_gpus in candidates:
                if nslots_target is not None and allocated_slots >= nslots_target:
                    break
                max_s = (
                    nslots_target - allocated_slots
                    if nslots_target is not None
                    else None
                )
                alloc_cores, alloc_gpus, n = self._affinity_alloc_from_rank(
                    rank,
                    free_cores,
                    free_gpus,
                    request,
                    max_slots=max_s,
                    exclusive=exclusive,
                )
                if n > 0:
                    selected.append((rank, alloc_cores, alloc_gpus))
                    allocated_slots += n
            if allocated_slots < nslots:
                self._check_feasibility(request)
                raise InsufficientResources("insufficient resources")
            actual_nslots = allocated_slots

        return selected, actual_nslots


class TreePool(ResourcePool):
    """Version-dispatching pool with sub-node affinity-aware GPU+core allocation.

    Wraps :class:`_TreePoolV1` for Rv1 resources.  Set
    :attr:`~flux.scheduler.Scheduler.pool_class` to this class or pass
    ``pool-class=TreePool`` to sched-simple.

    To support a future R version, add a version-specific implementation
    class and extend ``_impl_map``.
    """

    _impl_map = {1: _TreePoolV1}

    def __init__(self, R, log=None, **kwargs):
        if not isinstance(R, ResourcePoolImplementation):
            if isinstance(R, str):
                R = json.loads(R)
            version = R.get("version", 1) if isinstance(R, Mapping) else 1
            impl_class = self._impl_map.get(version)
            if impl_class is None:
                raise ValueError(f"R version {version} not supported by TreePool")
            R = impl_class(R, log=log, **kwargs)
        super().__init__(R)


pool_class = TreePool
