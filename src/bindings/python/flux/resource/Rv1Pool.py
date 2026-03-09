###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Pure-Python Rv1 resource pool for use as Scheduler.resource_class.

This module provides :class:`Rv1Pool`, a pure-Python implementation of the
:class:`~flux.scheduler.ResourcePool` protocol that can be used in place of
:class:`~flux.resource.Rlist` to demonstrate swapping the resource pool
implementation — as one would do when adding Rv2 support.

The internal representation mirrors the Rv1 R JSON wire format:

- ``_ranks``: per-rank dict with hostname, total core/GPU counts, allocated
  core/GPU counts, and up/down state.
- ``_properties``: mapping from property name to the set of ranks that have
  that property (parsed once from the initial ``resource.acquire`` response
  and treated as static, since the streaming RPC never re-sends properties).

Limitations:

- Only worst-fit allocation is implemented; other modes are rejected
  with ``EINVAL``.
- Core and GPU assignment within a node uses slots [0, nallocated-1];
  topology is not considered.

To use this class as the resource pool in a scheduler::

    from flux.resource.Rv1Pool import Rv1Pool

    class MyScheduler(Scheduler):
        resource_class = Rv1Pool
        ...

Or at load time::

    flux module load sched-fifo resource-class=Rv1Pool
"""

import errno
import json
import time
from collections.abc import Mapping
from typing import Dict, List, Optional, Set, Tuple

from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource.ResourcePool import ResourcePool


class ResourceRequest:
    """Parsed resource request extracted from a V1 jobspec.

    Returned by :meth:`Rv1Pool.parse_resource_request` and stored on
    :class:`~flux.scheduler.PendingJob`.  Passed to
    :meth:`Rv1Pool.alloc` so that the pool does not need to
    re-parse jobspec on every scheduling pass.

    Attributes:
        nnodes (int): Minimum node count; 0 means any layout.
        nslots (int): Total slot count.
        slot_size (int): Cores per slot.
        gpu_per_slot (int): GPUs per slot.
        duration (float): Walltime in seconds; 0.0 means unlimited.
        constraint: RFC 31 constraint expression (dict) or None.
        exclusive (bool): Whole-node exclusive allocation.
    """

    __slots__ = (
        "nnodes",
        "nslots",
        "slot_size",
        "gpu_per_slot",
        "duration",
        "constraint",
        "exclusive",
    )

    def __init__(
        self, nnodes, nslots, slot_size, gpu_per_slot, duration, constraint, exclusive
    ):
        self.nnodes = nnodes
        self.nslots = nslots
        self.slot_size = slot_size
        self.gpu_per_slot = gpu_per_slot
        self.duration = duration
        self.constraint = constraint
        self.exclusive = exclusive

    @classmethod
    def from_jobspec(cls, jobspec):
        """Parse a V1 jobspec dict and return a :class:`ResourceRequest`.

        Handles the two common V1 layouts:

        - ``node → slot → core[+gpu]``  (``flux submit -N<n> ...``)
        - ``slot → core[+gpu]``          (``flux submit -n<n> ...``)

        Raises:
            ValueError: If the jobspec cannot be parsed.
            KeyError: If required fields are missing.
        """
        resources = jobspec.get("resources", [])
        if not resources:
            raise ValueError("jobspec has no resources")
        top = resources[0]
        rtype = top.get("type")
        if rtype == "node":
            nnodes = top.get("count", 1)
            slot = top["with"][0]
            nslots_per_node = slot.get("count", 1)
            nslots = nslots_per_node * nnodes
            slot_children = slot["with"]
        elif rtype == "slot":
            nnodes = 0
            nslots = top.get("count", 1)
            slot_children = top["with"]
        else:
            raise ValueError(f"unsupported top-level resource type: {rtype!r}")

        slot_size = 1
        gpu_per_slot = 0
        for child in slot_children:
            if child.get("type") == "core":
                slot_size = child.get("count", 1)
            elif child.get("type") == "gpu":
                gpu_per_slot = child.get("count", 1)

        exclusive = bool(top.get("exclusive", False))
        # RFC 25: in jobspec V1, attributes.system and duration are required.
        system = jobspec.get("attributes", {}).get("system")
        if jobspec.get("version") == 1:
            if system is None:
                raise ValueError("getting duration: Object item not found: system")
            if system.get("duration") is None:
                raise ValueError("getting duration: Object item not found: duration")
        attrs = system or {}
        duration = attrs.get("duration") or 0.0
        constraint = attrs.get("constraints")
        return cls(
            nnodes,
            nslots,
            slot_size,
            gpu_per_slot,
            float(duration),
            constraint,
            exclusive,
        )

    @property
    def ncores(self):
        """Total cores requested (nslots × slot_size)."""
        return self.nslots * self.slot_size

    @property
    def ngpus(self):
        """Total GPUs requested (nslots × gpu_per_slot)."""
        return self.nslots * self.gpu_per_slot


def _compact_idset(ints) -> str:
    """Return a compact RFC 22 idset string, with brackets for multi-element sets.

    Single element: ``"3"``; multiple elements: ``"[0-2]"`` or ``"[0,2,4]"``.
    """
    ids = sorted(set(ints))
    if not ids:
        return ""
    ranges: List[str] = []
    start = end = ids[0]
    for n in ids[1:]:
        if n == end + 1:
            end = n
        else:
            ranges.append(str(start) if start == end else f"{start}-{end}")
            start = end = n
    ranges.append(str(start) if start == end else f"{start}-{end}")
    inner = ",".join(ranges)
    return inner if len(ids) == 1 else f"[{inner}]"


class Rv1Pool(ResourcePool):
    """Pure-Python implementation of the ResourcePool protocol for Rv1.

    Supports both CPU and GPU resources.  Set as the resource pool class
    on a scheduler subclass to enable GPU scheduling::

        class MyScheduler(Scheduler):
            resource_class = Rv1Pool

    See :class:`~flux.scheduler.ResourcePool` for the protocol contract.
    """

    #: Indicates to schedulers that GPU allocation is supported.
    supports_gpu = True

    #: Allocation modes accepted by :meth:`alloc`.
    supported_alloc_modes = frozenset({"worst-fit"})

    def __init__(self, R) -> None:
        """Construct from an R JSON string or dict (Rv1 format)."""
        if isinstance(R, str):
            R = json.loads(R)
        elif not isinstance(R, Mapping):
            raise TypeError(f"Rv1Pool: expected str or Mapping, got {type(R)!r}")

        execution = R.get("execution", {})
        self._expiration: float = float(execution.get("expiration") or 0.0)
        self._starttime: float = float(execution.get("starttime") or 0.0)

        # The nodelist array may contain hostlist expressions (e.g. "sun[1,3]")
        # rather than individual hostnames.  Expand each entry so we have one
        # hostname string per rank, in rank order.
        expanded_nodelist: List[str] = []
        for entry in execution.get("nodelist", []):
            expanded_nodelist.extend(list(Hostlist(entry)))

        # _ranks: rank -> {hostname, ncores, nallocated, ngpus, ngpus_allocated, up}
        self._ranks: Dict[int, dict] = {}
        for entry in execution.get("R_lite", []):
            rank_ids = IDset(entry["rank"])
            children = entry.get("children", {})
            ncores = len(list(IDset(children["core"]))) if "core" in children else 0
            ngpus = len(list(IDset(children["gpu"]))) if "gpu" in children else 0
            for rank in rank_ids:
                hostname = (
                    expanded_nodelist[rank]
                    if rank < len(expanded_nodelist)
                    else f"rank{rank}"
                )
                self._ranks[rank] = {
                    "hostname": hostname,
                    "ncores": ncores,
                    "nallocated": 0,
                    "ngpus": ngpus,
                    "ngpus_allocated": 0,
                    "up": True,
                }

        # _properties: property name -> set of rank ints (static after init)
        self._properties: Dict[str, Set[int]] = {
            prop: set(IDset(ids_str))
            for prop, ids_str in execution.get("properties", {}).items()
        }

        # _job_state: jobid -> (end_time, alloc) for all jobs currently tracked
        # by this pool.  Populated by register_alloc() and alloc(); cleared by
        # free().  Only the main scheduler pool carries job state; copies and
        # alloc objects returned by alloc() start with an empty dict.
        self._job_state: Dict[int, Tuple[float, "Rv1Pool"]] = {}

    # ------------------------------------------------------------------
    # ResourcePool protocol
    # ------------------------------------------------------------------

    def mark_up(self, ids: str) -> None:
        """Mark ranks as schedulable."""
        if ids == "all":
            for info in self._ranks.values():
                info["up"] = True
        else:
            for rank in IDset(ids):
                if rank in self._ranks:
                    self._ranks[rank]["up"] = True
        self._bump()

    def mark_down(self, ids: str) -> None:
        """Mark ranks as not schedulable."""
        if ids == "all":
            for info in self._ranks.values():
                info["up"] = False
        else:
            for rank in IDset(ids):
                if rank in self._ranks:
                    self._ranks[rank]["up"] = False
        self._bump()

    @property
    def expiration(self) -> float:
        return self._expiration

    @expiration.setter
    def expiration(self, value: float) -> None:
        self._expiration = float(value)

    def remove_ranks(self, ranks) -> None:
        """Remove ranks from the pool (shrink event)."""
        for rank in ranks:
            self._ranks.pop(rank, None)
        self._bump()

    def _set_allocated(self, other: "Rv1Pool") -> None:
        """Mark resources in *other* as allocated in this pool (internal helper)."""
        for rank, oinfo in other._ranks.items():
            if rank in self._ranks:
                self._ranks[rank]["nallocated"] += oinfo["ncores"]
                self._ranks[rank]["ngpus_allocated"] += oinfo["ngpus"]

    def register_alloc(self, jobid: int, R: "Rv1Pool") -> None:
        """Register an existing allocation with the pool.

        Used during scheduler reconnect to record allocations that were made
        before the scheduler restarted.  Marks the resources in *R* as
        allocated and begins tracking *jobid*.

        Args:
            jobid (int): The job ID.
            R: The job's current allocation (an :class:`Rv1Pool` instance
                constructed from the job's R record).  Its
                :attr:`~Rv1Pool.expiration` is used as the end time.
        """
        self._set_allocated(R)
        self._job_state[jobid] = (R.expiration, R)
        self._bump()

    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the end time for a tracked job.

        Called when job-manager issues a ``sched.expiration`` update.  The
        The new *expiration* is stored in :attr:`_job_state`.

        Args:
            jobid (int): The job ID.
            expiration (float): New expiration timestamp (seconds since epoch).
        """
        if jobid in self._job_state:
            _, alloc = self._job_state[jobid]
            self._job_state[jobid] = (expiration, alloc)
            self._bump()

    def check_feasibility(self, request) -> None:
        """Check whether *request* is structurally satisfiable.

        Allocates from a clean copy of the pool (all resources up and
        unallocated) to determine whether the request could ever be satisfied,
        regardless of current availability.

        Raises:
            OSError: If the request cannot be satisfied even when all resources
                are available (e.g. requests more nodes than exist).
        """
        test = self.copy()
        test.mark_up("all")
        test.alloc(0, request)

    def deepcopy(self) -> "Rv1Pool":
        """Return a full independent copy preserving allocation state."""
        new = object.__new__(Rv1Pool)
        new.generation = self.generation
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._properties = {p: set(s) for p, s in self._properties.items()}
        # Manually copy _ranks rather than using copy.deepcopy: on large systems
        # (e.g. 16K nodes) copy.deepcopy() is O(N) with a high constant due to
        # per-object memo tracking and type dispatch.  The inner dicts contain
        # only primitives (str, int, bool) so a shallow dict() copy is correct.
        new._ranks = {rank: dict(info) for rank, info in self._ranks.items()}
        # Alloc objects also carry a _ranks dict (their allocated nodes only);
        # delegate to their own deepcopy() to apply the same fast-copy path.
        new._job_state = {
            jid: (end_time, alloc.deepcopy())
            for jid, (end_time, alloc) in self._job_state.items()
        }
        return new

    def copy(self) -> "Rv1Pool":
        """Return a structural copy with allocation state cleared."""
        new = object.__new__(Rv1Pool)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._properties = {p: set(s) for p, s in self._properties.items()}
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "ncores": info["ncores"],
                "nallocated": 0,
                "ngpus": info["ngpus"],
                "ngpus_allocated": 0,
                "up": info["up"],
            }
            for rank, info in self._ranks.items()
        }
        new._job_state = {}
        return new

    def copy_allocated(self) -> "Rv1Pool":
        """Return a pool containing only the allocated resources."""
        new = object.__new__(Rv1Pool)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._properties = {}
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "ncores": info["nallocated"],
                "nallocated": info["nallocated"],
                "ngpus": info["ngpus_allocated"],
                "ngpus_allocated": info["ngpus_allocated"],
                "up": True,
            }
            for rank, info in self._ranks.items()
            if info["nallocated"] > 0 or info["ngpus_allocated"] > 0
        }
        new._job_state = {}
        return new

    def copy_down(self) -> "Rv1Pool":
        """Return a pool containing only the down (not schedulable) ranks."""
        new = object.__new__(Rv1Pool)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._properties = {}
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "ncores": info["ncores"],
                "nallocated": 0,
                "ngpus": info["ngpus"],
                "ngpus_allocated": 0,
                "up": False,
            }
            for rank, info in self._ranks.items()
            if not info["up"]
        }
        new._job_state = {}
        return new

    def encode(self) -> str:
        """Serialize to an R JSON string."""
        return json.dumps(self._build_dict())

    # ------------------------------------------------------------------
    # Scheduler-used methods (not part of the ResourcePool protocol)
    # ------------------------------------------------------------------

    def count(self, name: str) -> int:
        """Return total count of *name* across all ranks.

        Supports ``"core"`` and ``"gpu"``; returns 0 for other names.
        """
        if name == "core":
            return sum(info["ncores"] for info in self._ranks.values())
        if name == "gpu":
            return sum(info["ngpus"] for info in self._ranks.values())
        return 0

    def set_starttime(self, starttime: float) -> None:
        """Set the resource set starttime (seconds since epoch)."""
        self._starttime = float(starttime)

    def set_expiration(self, expiration: float) -> None:
        """Set the resource set expiration (seconds since epoch, 0 = none)."""
        self._expiration = float(expiration)

    def dumps(self) -> str:
        """Return a compact human-readable summary.

        Format: ``rank<idset>/core<idset>[+gpu<idset>][,...]``
        """
        # Group ranks by (ncores, ngpus) for compact output
        by_resources: Dict[Tuple[int, int], List[int]] = {}
        for rank, info in self._ranks.items():
            key = (info["ncores"], info["ngpus"])
            by_resources.setdefault(key, []).append(rank)

        parts = []
        for (nc, ng), ranks in sorted(by_resources.items()):
            rank_str = "rank" + _compact_idset(ranks)
            core_str = "core" + _compact_idset(range(nc))
            s = f"{rank_str}/{core_str}"
            if ng > 0:
                s += "+gpu" + _compact_idset(range(ng))
            parts.append(s)
        return ",".join(parts)

    def to_dict(self) -> dict:
        """Return the resource set as a parsed R JSON dict."""
        return self._build_dict()

    def parse_resource_request(self, jobspec):
        """Parse a V1 jobspec and return a :class:`~flux.resource.Rv1Pool.ResourceRequest`."""
        return ResourceRequest.from_jobspec(jobspec)

    def alloc(
        self,
        jobid: int,
        request,
        mode: Optional[str] = None,
    ) -> "Rv1Pool":
        """Allocate resources for *jobid* matching *request*.

        Only worst-fit is implemented.

        Args:
            jobid (int): The job ID; stored in :attr:`_job_state` so that
                :meth:`free` can find it.
            request: A :class:`~flux.resource.Rv1Pool.ResourceRequest`
                describing the resources needed.
            mode: Allocation strategy; ``None`` or ``"worst-fit"``.  Only
                worst-fit is implemented.

        Returns:
            A new :class:`Rv1Pool` containing the allocated resources.

        Raises:
            OSError(errno.ENOSPC): Resources temporarily insufficient.
            OSError(errno.EOVERFLOW): Request structurally infeasible.
        """
        nnodes = request.nnodes
        nslots = request.nslots
        slot_size = request.slot_size
        gpu_per_slot = request.gpu_per_slot
        exclusive = request.exclusive
        constraint = request.constraint

        if mode is not None and mode != "worst-fit":
            raise OSError(errno.EINVAL, f"unsupported alloc mode {mode!r}")

        if constraint is not None and isinstance(constraint, str):
            constraint = json.loads(constraint)

        # Build candidate list: up ranks with enough free cores (and GPUs)
        candidates: List[Tuple[int, dict]] = []
        for rank, info in self._ranks.items():
            if not info["up"]:
                continue
            free_cores = info["ncores"] - info["nallocated"]
            needed_cores = info["ncores"] if exclusive else slot_size
            if free_cores < needed_cores:
                continue
            if gpu_per_slot > 0:
                free_gpus = info["ngpus"] - info["ngpus_allocated"]
                if free_gpus < gpu_per_slot:
                    continue
            candidates.append((rank, info))

        if constraint is not None:
            candidates = [
                (r, i)
                for r, i in candidates
                if self._matches_constraint(r, i, constraint)
            ]

        # Worst-fit: descending free cores (break ties by free GPUs)
        candidates.sort(
            key=lambda x: (
                x[1]["ncores"] - x[1]["nallocated"],
                x[1]["ngpus"] - x[1]["ngpus_allocated"],
            ),
            reverse=True,
        )

        # Raise EOVERFLOW for structurally infeasible requests
        self._check_feasibility(
            nnodes, nslots, slot_size, exclusive, constraint, gpu_per_slot
        )

        # Greedy selection
        selected: List[Tuple[int, dict, int, int]] = []  # rank, info, cores, gpus

        if nnodes > 0:
            slots_per_node = nslots // nnodes
            for rank, info in candidates:
                if len(selected) >= nnodes:
                    break
                if exclusive:
                    need_cores = info["ncores"]
                    need_gpus = info["ngpus"]
                else:
                    need_cores = slots_per_node * slot_size
                    need_gpus = slots_per_node * gpu_per_slot
                free_cores = info["ncores"] - info["nallocated"]
                free_gpus = info["ngpus"] - info["ngpus_allocated"]
                if free_cores >= need_cores and free_gpus >= need_gpus:
                    selected.append((rank, info, need_cores, need_gpus))
            if len(selected) < nnodes:
                raise OSError(errno.ENOSPC, "insufficient resources")
        else:
            remaining_slots = nslots
            for rank, info in candidates:
                if remaining_slots <= 0:
                    break
                free_core_slots = (info["ncores"] - info["nallocated"]) // slot_size
                if gpu_per_slot > 0:
                    free_gpu_slots = (
                        info["ngpus"] - info["ngpus_allocated"]
                    ) // gpu_per_slot
                    free_slots = min(free_core_slots, free_gpu_slots)
                else:
                    free_slots = free_core_slots
                take = min(free_slots, remaining_slots)
                if take > 0:
                    selected.append((rank, info, take * slot_size, take * gpu_per_slot))
                    remaining_slots -= take
            if remaining_slots > 0:
                raise OSError(errno.ENOSPC, "insufficient resources")

        # Build result pool and update allocation state on self
        result = object.__new__(Rv1Pool)
        result._expiration = 0.0
        result._starttime = 0.0
        result._properties = {}
        result._ranks = {}
        result._job_state = {}

        for rank, info, ncores_alloc, ngpus_alloc in selected:
            result._ranks[rank] = {
                "hostname": info["hostname"],
                "ncores": ncores_alloc,
                "nallocated": ncores_alloc,
                "ngpus": ngpus_alloc,
                "ngpus_allocated": ngpus_alloc,
                "up": True,
            }
            info["nallocated"] += ncores_alloc
            info["ngpus_allocated"] += ngpus_alloc

        # Compute end time from the request's duration, falling back to the
        # pool-level expiration.  Stored in _job_state for estimate_start_time.
        if request.duration > 0.0:
            end_time = time.time() + request.duration
        elif self._expiration > 0.0:
            end_time = self._expiration
        else:
            end_time = 0.0
        self._job_state[jobid] = (end_time, result)
        self._bump()
        return result

    def free(self, jobid: int) -> None:
        """Return a job's allocated resources to this pool.

        Looks up the allocation by *jobid* in :attr:`_job_state`, removes the
        entry, and returns the resources to the free pool.

        Args:
            jobid (int): The job ID passed to :meth:`alloc` or
                :meth:`register_alloc`.
        """
        _, alloc = self._job_state.pop(jobid, (0.0, None))
        if alloc is None:
            return
        for rank, ainfo in alloc._ranks.items():
            if rank in self._ranks:
                self._ranks[rank]["nallocated"] = max(
                    0, self._ranks[rank]["nallocated"] - ainfo["ncores"]
                )
                self._ranks[rank]["ngpus_allocated"] = max(
                    0, self._ranks[rank]["ngpus_allocated"] - ainfo["ngpus"]
                )
        self._bump()

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _check_feasibility(
        self,
        nnodes: int,
        nslots: int,
        slot_size: int,
        exclusive: bool,
        constraint,
        gpu_per_slot: int,
    ) -> None:
        """Raise EOVERFLOW if the request can structurally never be satisfied."""
        all_candidates = list(self._ranks.items())
        if constraint is not None:
            all_candidates = [
                (r, i)
                for r, i in all_candidates
                if self._matches_constraint(r, i, constraint)
            ]

        if exclusive:
            need_nodes = max(nnodes, 1)
            eligible = [
                (r, i)
                for r, i in all_candidates
                if i["ncores"] >= slot_size
                and (gpu_per_slot == 0 or i["ngpus"] >= gpu_per_slot)
            ]
            if len(eligible) < need_nodes:
                raise OSError(
                    errno.EOVERFLOW,
                    f"unsatisfiable request: need {need_nodes} exclusive node(s), "
                    f"only {len(eligible)} eligible",
                )
            return

        if nnodes > 0:
            slots_per_node = nslots // nnodes
            cores_per_node = slots_per_node * slot_size
            gpus_per_node = slots_per_node * gpu_per_slot
            eligible = [
                (r, i)
                for r, i in all_candidates
                if i["ncores"] >= cores_per_node and i["ngpus"] >= gpus_per_node
            ]
            if len(eligible) < nnodes:
                raise OSError(
                    errno.EOVERFLOW,
                    f"unsatisfiable request: need {nnodes} node(s) with "
                    f"{cores_per_node} cores"
                    + (f" and {gpus_per_node} GPUs" if gpus_per_node else "")
                    + f" each, only {len(eligible)} eligible",
                )
            return

        total_cores = sum(i["ncores"] for _, i in all_candidates)
        needed_cores = nslots * slot_size
        if needed_cores > total_cores:
            raise OSError(
                errno.EOVERFLOW,
                f"unsatisfiable request: need {needed_cores} cores, "
                f"only {total_cores} available in total",
            )
        if gpu_per_slot > 0:
            total_gpus = sum(i["ngpus"] for _, i in all_candidates)
            needed_gpus = nslots * gpu_per_slot
            if needed_gpus > total_gpus:
                raise OSError(
                    errno.EOVERFLOW,
                    f"unsatisfiable request: need {needed_gpus} GPUs, "
                    f"only {total_gpus} available in total",
                )

    def _matches_constraint(self, rank: int, info: dict, constraint: dict) -> bool:
        """Return True if *rank* satisfies the RFC 31 *constraint* expression."""
        if "properties" in constraint:
            return all(
                prop in self._properties and rank in self._properties[prop]
                for prop in constraint["properties"]
            )
        if "hostlist" in constraint:
            return info["hostname"] in list(Hostlist(constraint["hostlist"]))
        if "ranks" in constraint:
            return rank in IDset(constraint["ranks"])
        if "and" in constraint:
            return all(
                self._matches_constraint(rank, info, c) for c in constraint["and"]
            )
        if "or" in constraint:
            return any(
                self._matches_constraint(rank, info, c) for c in constraint["or"]
            )
        if "not" in constraint:
            return not self._matches_constraint(rank, info, constraint["not"][0])
        return True  # unknown operator — permissive

    def _build_dict(self) -> dict:
        """Build the R JSON dict from internal state."""
        # Flat list of hostnames in rank order — one entry per rank,
        # matching the count of R_lite entries so rlist_assign_hostlist
        # can pair them correctly.
        nodelist = [info["hostname"] for _, info in sorted(self._ranks.items())]

        # Group ranks by (ncores, ngpus) for compact R_lite entries
        by_resources: Dict[Tuple[int, int], List[int]] = {}
        for rank, info in self._ranks.items():
            key = (info["ncores"], info["ngpus"])
            by_resources.setdefault(key, []).append(rank)

        R_lite = []
        for (nc, ng), ranks in sorted(by_resources.items()):
            children: dict = {"core": _compact_idset(range(nc))}
            if ng > 0:
                children["gpu"] = _compact_idset(range(ng))
            R_lite.append(
                {
                    "rank": _compact_idset(ranks),
                    "children": children,
                }
            )

        result: dict = {
            "version": 1,
            "execution": {
                "R_lite": R_lite,
                "starttime": self._starttime,
                "expiration": self._expiration,
                "nodelist": nodelist,
            },
        }
        if self._properties:
            result["execution"]["properties"] = {
                prop: _compact_idset(sorted(ranks))
                for prop, ranks in self._properties.items()
            }
        return result
