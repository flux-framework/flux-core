###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Rv1 resource pool: combined resource-set and scheduler-layer implementation.

:class:`Rv1Pool` inherits from both :class:`~flux.resource.Rv1Set.Rv1Set`
(resource-set tracking) and
:class:`~flux.resource.ResourcePoolImplementation.ResourcePoolImplementation`
(scheduler-layer protocol).  It adds per-rank availability and allocation
tracking to the base Rv1Set representation.

The internal ``_ranks`` dict is extended with three extra keys beyond what
``Rv1Set`` uses::

    {"hostname": str, "cores": frozenset, "gpus": frozenset,
     "up": bool, "allocated_cores": set, "allocated_gpus": set}

``Rv1Pool`` supports GPU scheduling and worst-fit allocation.  Constraints (properties, hostlist, ranks,
boolean operators) are supported via the inherited
:meth:`~flux.resource.Rv1Set.Rv1Set._matches_constraint` method.

To obtain an ``Rv1Pool`` from an R JSON string or dict, call::

    from flux.resource import ResourcePool
    pool = ResourcePool(R)          # factory dispatch → Rv1Pool

or directly::

    from flux.resource.Rv1Pool import Rv1Pool
    pool = Rv1Pool(R)
"""

import json
import syslog
import time
from collections.abc import Mapping
from typing import Dict, List, Tuple

from flux.idset import IDset
from flux.job import JobID
from flux.resource.ResourceCount import ResourceCount
from flux.resource.ResourcePoolImplementation import (
    InfeasibleRequest,
    InsufficientResources,
    ResourcePoolImplementation,
)
from flux.resource.Rv1Set import Rv1Set


class ResourceRequest:
    """Parsed resource request extracted from a V1 jobspec.

    Returned by :meth:`Rv1Pool.parse_resource_request` and stored on
    :class:`~flux.scheduler.PendingJob`.  Passed to
    :meth:`Rv1Pool.alloc` so that the pool does not need to
    re-parse jobspec on every scheduling pass.

    Attributes:
        node_count (ResourceCount | None): RFC 14 node count (scaled by
            nodefactor), or ``None`` for slot-only layouts.  For exclusive
            slot-only jobspecs this holds the slot count reinterpreted as
            node count.
        slot_count (ResourceCount): RFC 14 slot count.  For node-based layouts
            this is the per-node slot count; for slot-only layouts it is the
            total slot count; for exclusive slot-only layouts it is
            ``ResourceCount(1, 1)``.
        slot_size (int): Cores per slot.
        gpu_per_slot (int): GPUs per slot.
        duration (float): Walltime in seconds; 0.0 means unlimited.
        constraint: RFC 31 constraint expression (dict) or None.
        exclusive (bool): Whole-node exclusive allocation.
        nnodes (int): Minimum node count; derived from *node_count*.
        nnodes_max (int | None): Maximum node count; ``None`` for unbounded.
        nslots (int): Minimum total slot count; derived from *slot_count* and
            *node_count*.
        nslots_max (int | None): Maximum total slot count.
    """

    __slots__ = (
        "node_count",
        "slot_count",
        "slot_size",
        "gpu_per_slot",
        "duration",
        "constraint",
        "exclusive",
    )

    def __init__(
        self,
        node_count,
        slot_count,
        slot_size,
        gpu_per_slot,
        duration,
        constraint,
        exclusive,
    ):
        self.node_count = node_count
        self.slot_count = slot_count
        self.slot_size = slot_size
        self.gpu_per_slot = gpu_per_slot
        self.duration = duration
        self.constraint = constraint
        self.exclusive = exclusive

    @property
    def nnodes(self):
        """Minimum node count; 0 for slot-only layouts."""
        return self.node_count.min if self.node_count is not None else 0

    @property
    def nnodes_max(self):
        """Maximum node count; 0 for slot-only; None for unbounded."""
        return self.node_count.max if self.node_count is not None else 0

    @property
    def nslots(self):
        """Minimum total slot count."""
        if self.node_count is None:
            return self.slot_count.min
        return self.slot_count.min * self.node_count.min

    @property
    def nslots_max(self):
        """Maximum total slot count; None for unbounded."""
        if self.node_count is None:
            return self.slot_count.max
        if self.node_count.max is None or self.slot_count.max is None:
            return None
        return self.slot_count.max * self.node_count.max

    @classmethod
    def from_jobspec(cls, jobspec):
        """Parse a jobspec dict and return a :class:`ResourceRequest`.

        Walks the resource graph recursively, like libjjc, to support both
        RFC 25 V1 jobspecs and jobspecs with non-V1 resource hierarchies.
        Unknown resource types are skipped but their ``with`` children are
        still traversed.  The version key value is not checked (see #6682).

        RFC 14 range counts in all forms (integer, dict, RFC 45 string, idset
        string) on the node or slot resource are fully supported: the scheduler
        allocates as many resources as available up to the maximum.

        Raises:
            ValueError: If the jobspec cannot be parsed.
            KeyError: If required fields are missing.
        """
        resources = jobspec.get("resources", [])
        if not resources:
            raise ValueError("jobspec has no resources")

        # State accumulated during the recursive walk (last-write-wins,
        # matching libjjc behavior).
        state = {
            "nnodes": None,  # ResourceCount or None if no node vertex found
            "nslots": None,  # ResourceCount or None if no slot vertex found
            "slot_size": None,  # None until a core vertex is found
            "gpu_per_slot": 0,
            "exclusive": False,
            "nodefactor": 1,  # product of non-node counts above node level
        }

        def walk(res_list, nodefactor):
            for vertex in res_list:
                rtype = vertex.get("type", "")
                count = ResourceCount.from_count_spec(vertex.get("count", 1))
                children = vertex.get("with", [])
                if rtype == "node":
                    state["nnodes"] = count
                    state["nodefactor"] = nodefactor
                    if vertex.get("exclusive", False):
                        state["exclusive"] = True
                    if children:
                        walk(children, nodefactor)
                else:
                    # Non-node: accumulate nodefactor (use min for ranges),
                    # then record known types and recurse.
                    new_nf = nodefactor * count.min
                    if rtype == "slot":
                        state["nslots"] = count
                        if vertex.get("exclusive", False):
                            state["exclusive"] = True
                    elif rtype == "core":
                        state["slot_size"] = count.min
                    elif rtype == "gpu":
                        state["gpu_per_slot"] = count.min
                    # else: unknown type — ignore, continue recursing
                    if children:
                        walk(children, new_nf)

        walk(resources, 1)

        # RFC 25: in jobspec V1, attributes.system and duration are required.
        # Check this before validating the resource structure so that the error
        # message matches what the C jj/jjc parsers produce for V1 jobspecs.
        system = jobspec.get("attributes", {}).get("system")
        if jobspec.get("version") == 1:
            if system is None:
                raise ValueError("getting duration: Object item not found: system")
            if system.get("duration") is None:
                raise ValueError("getting duration: Object item not found: duration")

        if state["nslots"] is None:
            raise ValueError("Unable to determine slot count")
        if state["slot_size"] is None:
            raise ValueError("Unable to determine slot size")

        nf = state["nodefactor"]
        sc = state["nslots"]  # ResourceCount for the slot vertex

        if state["nnodes"] is not None:
            node_count = state["nnodes"].scaled(nf)
            slot_count = sc  # per-node slot ResourceCount
        else:
            node_count = None
            slot_count = sc

        exclusive = state["exclusive"]
        # Exclusive allocation is per-node; if no node vertex was specified
        # (slot-only jobspec), each slot occupies one exclusive node.
        if exclusive and node_count is None:
            node_count = slot_count  # slot count reinterpreted as node count
            slot_count = ResourceCount(1, 1)  # 1 slot per exclusive node
        attrs = system or {}
        duration = attrs.get("duration") or 0.0
        constraint = attrs.get("constraints") or None
        return cls(
            node_count,
            slot_count,
            state["slot_size"],
            state["gpu_per_slot"],
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


class Rv1Pool(Rv1Set, ResourcePoolImplementation):
    """Pure-Python Rv1 resource pool combining resource-set and scheduler layers.

    MRO: ``Rv1Pool → Rv1Set → ResourceSetImplementation →
    ResourcePoolImplementation → ABC → object``

    The ``_ranks`` dict carries three extra pool-specific keys beyond the
    base :class:`~flux.resource.Rv1Set.Rv1Set` representation::

        up              – True if rank is schedulable
        allocated_cores – set of core IDs currently allocated on this rank
        allocated_gpus  – set of GPU IDs currently allocated on this rank
    """

    version = 1

    #: Pool options recognised by this implementation.  The scheduler logs
    #: a warning for any key in ``pool_kwargs`` not listed here.
    known_options: frozenset = frozenset()

    def __init__(self, R, log=None, **kwargs) -> None:
        """Construct from an R JSON string, dict, or ``None`` (empty)."""
        if log is not None:
            self.log = log
        if R is not None and not isinstance(R, (str, Mapping)):
            raise TypeError(f"Rv1Pool: expected str or Mapping, got {type(R)!r}")

        # Rv1Set.__init__ parses the core resource structure.  Pass
        # keep_scheduling=True so R.scheduling is retained for propagation
        # to allocations per RFC 20 §scheduling, without a second JSON parse.
        super().__init__(R, keep_scheduling=True)

        # Add pool-specific fields to each rank entry.
        for info in self._ranks.values():
            info["up"] = True
            info["allocated_cores"] = set()
            info["allocated_gpus"] = set()

        # _job_state: jobid -> (end_time, alloc) for all tracked jobs.
        self._job_state: Dict[int, Tuple[float, "Rv1Pool"]] = {}

    # ------------------------------------------------------------------
    # Rv1Set override: _copy_from_ranks must add pool-specific fields
    # ------------------------------------------------------------------

    def _copy_from_ranks(self, rank_set: set) -> "Rv1Pool":
        """Return a new Rv1Pool containing only the given ranks (alloc cleared)."""
        new = object.__new__(Rv1Pool)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = getattr(self, "_has_nodelist", False)
        new._properties = {
            prop: ranks & rank_set
            for prop, ranks in self._properties.items()
            if ranks & rank_set
        }
        new._ranks = {
            rank: {
                "hostname": self._ranks[rank]["hostname"],
                "cores": self._ranks[rank]["cores"],
                "gpus": self._ranks[rank]["gpus"],
                "up": self._ranks[rank]["up"],
                "allocated_cores": set(),
                "allocated_gpus": set(),
            }
            for rank in rank_set
            if rank in self._ranks
        }
        new.scheduling = self.scheduling
        new._job_state = {}
        new.log = self.log
        return new

    # ------------------------------------------------------------------
    # ResourcePoolImplementation — availability management
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

    def remove_ranks(self, ranks) -> None:
        """Remove ranks from the pool (shrink event)."""
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        for rank in ranks:
            self._ranks.pop(rank, None)
            for prop_ranks in self._properties.values():
                prop_ranks.discard(rank)
        self._bump()

    # ------------------------------------------------------------------
    # ResourcePoolImplementation — job lifecycle
    # ------------------------------------------------------------------

    def register_alloc(self, jobid: int, R: "Rv1Pool") -> None:
        """Register an existing allocation during scheduler reconnect.

        Raises:
            ValueError: If R contains ranks absent from this pool, which
                indicates the job's resources are no longer available (e.g.
                the rank was excluded from configuration before the scheduler
                was reloaded).  The caller should raise a fatal exception on
                the job.
        """
        missing = set(R._ranks.keys()) - set(self._ranks.keys())
        if missing:
            raise ValueError(
                f"allocation contains ranks not in resource pool: " f"{sorted(missing)}"
            )
        self._set_allocated(R)
        self._job_state[jobid] = (R.get_expiration(), R)
        self._bump()
        self.log(syslog.LOG_INFO, f"hello: {JobID(jobid).f58}: {R.dumps()}")

    def free(self, jobid: int, R=None, final: bool = False) -> None:
        """Return a job's allocated resources to this pool.

        The scheduler allocation protocol may partially release a job's
        resources by rank; each partial free updates both the pool and the
        job's tracked allocation in :attr:`_job_state`.

        Args:
            jobid: ID of the job whose resources are being freed.
            R: Resource set to free.  For a partial free this contains only
                the subset being freed; when ``final`` is True and ``R`` is
                provided it must exactly match the tracked allocation.
                If ``None``, all tracked resources for the job are freed.
            final: If True, all resources remaining in the tracked allocation
                are freed and the job state is removed.  For a partial free,
                ``R`` must be a subset of the tracked allocation.  These
                checks catch job manager accounting errors.

        Raises:
            ValueError: If ``R`` contains ranks not in the job's tracked
                allocation, or if ``final`` is True and ``R`` does not match
                the tracked allocation exactly.
        """
        if R is not None and jobid in self._job_state:
            _, alloc = self._job_state[jobid]
            extra = set(R._ranks.keys()) - set(alloc._ranks.keys())
            if extra:
                raise ValueError(
                    f"free: {JobID(jobid).f58}: R contains ranks not in "
                    f"job allocation: {extra}"
                )
            if final:
                missing = set(alloc._ranks.keys()) - set(R._ranks.keys())
                mismatched = {
                    rank
                    for rank in R._ranks
                    if R._ranks[rank]["cores"] != alloc._ranks[rank]["cores"]
                    or R._ranks[rank]["gpus"] != alloc._ranks[rank]["gpus"]
                }
                if missing or mismatched:
                    raise ValueError(
                        f"free: {JobID(jobid).f58}: final R mismatch: "
                        f"got {R.dumps()}, expected {alloc.dumps()}"
                    )
        if final:
            _, alloc = self._job_state.pop(jobid, (0.0, None))
            if alloc is None:
                return
            for rank, ainfo in alloc._ranks.items():
                if rank in self._ranks:
                    self._ranks[rank]["allocated_cores"] -= ainfo["cores"]
                    self._ranks[rank]["allocated_gpus"] -= ainfo["gpus"]
            freed_dumps = alloc.dumps()
        elif R is not None:
            for rank, ainfo in R._ranks.items():
                if rank in self._ranks:
                    self._ranks[rank]["allocated_cores"] -= ainfo["cores"]
                    self._ranks[rank]["allocated_gpus"] -= ainfo["gpus"]
            freed_dumps = R.dumps()
            if jobid in self._job_state:
                end_time, alloc = self._job_state[jobid]
                remaining = set(alloc._ranks.keys()) - set(R._ranks.keys())
                self._job_state[jobid] = (end_time, alloc._copy_from_ranks(remaining))
        else:
            _, alloc = self._job_state.pop(jobid, (0.0, None))
            if alloc is None:
                return
            for rank, ainfo in alloc._ranks.items():
                if rank in self._ranks:
                    self._ranks[rank]["allocated_cores"] -= ainfo["cores"]
                    self._ranks[rank]["allocated_gpus"] -= ainfo["gpus"]
            freed_dumps = alloc.dumps()
        self.log(
            syslog.LOG_DEBUG,
            f"free: {JobID(jobid).f58}: {freed_dumps}" + (" (final)" if final else ""),
        )
        self._bump()

    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the end time for a tracked job."""
        if jobid in self._job_state:
            _, alloc = self._job_state[jobid]
            self._job_state[jobid] = (expiration, alloc)
            self._bump()

    def job_end_times(self) -> List[Tuple[int, float]]:
        """Return a list of ``(jobid, end_time)`` pairs for all tracked jobs."""
        return [(jid, end_time) for jid, (end_time, _) in self._job_state.items()]

    # ------------------------------------------------------------------
    # ResourcePoolImplementation — scheduling operations
    # ------------------------------------------------------------------

    def parse_resource_request(self, jobspec: dict) -> ResourceRequest:
        """Parse a V1 jobspec and return a :class:`ResourceRequest`."""
        return ResourceRequest.from_jobspec(jobspec)

    def check_feasibility(self, request) -> None:
        """Check whether a request is structurally satisfiable.

        Tests against total pool capacity (ignoring current allocations
        and availability) so that transient conditions don't affect the
        result.

        Args:
            request: A :class:`ResourceRequest` from
                :meth:`parse_resource_request`.

        Raises:
            InfeasibleRequest: If the request can never be satisfied by
                this pool's total capacity.
        """
        # Reject stepped/IDset counts that this scheduler cannot honor.
        # The stepped count form encodes constraints (e.g. power-of-two node
        # counts) that the simple range allocator ignores; reject immediately
        # rather than silently violating the constraint.
        if request.node_count is not None and request.node_count._values is not None:
            raise InfeasibleRequest(
                "node count specifies discrete valid values that this scheduler "
                "does not support; use a simple min-max range instead"
            )
        if request.slot_count is not None and request.slot_count._values is not None:
            raise InfeasibleRequest(
                "slot count specifies discrete valid values that this scheduler "
                "does not support; use a simple min-max range instead"
            )
        self._check_feasibility(
            request.nnodes,
            request.nslots,
            request.slot_size,
            request.exclusive,
            request.constraint,
            request.gpu_per_slot,
        )

    def alloc(self, jobid: int, request) -> "Rv1Pool":
        """Allocate resources for *jobid* matching *request*.

        Args:
            jobid (int): The job ID; stored in :attr:`_job_state`.
            request: A :class:`ResourceRequest` from
                :meth:`parse_resource_request`.

        Returns:
            A new :class:`Rv1Pool` containing the allocated resources.

        Raises:
            InsufficientResources: Resources temporarily insufficient.
            InfeasibleRequest: Request structurally infeasible.
        """
        if request.node_count is not None and request.node_count._values is not None:
            raise InfeasibleRequest(
                "node count specifies discrete valid values that this scheduler "
                "does not support; use a simple min-max range instead"
            )
        if request.slot_count is not None and request.slot_count._values is not None:
            raise InfeasibleRequest(
                "slot count specifies discrete valid values that this scheduler "
                "does not support; use a simple min-max range instead"
            )
        nnodes = request.nnodes
        nslots = request.nslots
        slot_size = request.slot_size
        gpu_per_slot = request.gpu_per_slot
        exclusive = request.exclusive
        constraint = request.constraint

        if constraint is not None and isinstance(constraint, str):
            constraint = json.loads(constraint)

        # Build candidate list: up ranks with enough free cores (and GPUs).
        # Each entry is (rank, info, free_cores, free_gpus).
        candidates = []
        for rank, info in self._ranks.items():
            if not info["up"]:
                continue
            free_cores = info["cores"] - info["allocated_cores"]
            free_gpus = info["gpus"] - info["allocated_gpus"]
            needed_cores = len(info["cores"]) if exclusive else slot_size
            if len(free_cores) < needed_cores:
                continue
            if gpu_per_slot > 0 and len(free_gpus) < gpu_per_slot:
                continue
            candidates.append((rank, info, free_cores, free_gpus))

        if constraint is not None:
            candidates = [
                (r, i, fc, fg)
                for r, i, fc, fg in candidates
                if self._matches_constraint(r, i, constraint)
            ]

        # Worst-fit: descending free cores (break ties by free GPUs)
        candidates.sort(
            key=lambda x: (len(x[2]), len(x[3])),
            reverse=True,
        )

        # Greedy selection — each entry is (rank, info, alloc_cores, alloc_gpus)
        selected: List[Tuple[int, dict, frozenset, frozenset]] = []

        if nnodes > 0:
            nnodes_min = nnodes
            # nnodes_max: same as min → fixed; larger → bounded range; None → unbounded
            nnodes_target = request.nnodes_max
            slots_per_node = nslots // nnodes_min
            for rank, info, free_cores, free_gpus in candidates:
                if nnodes_target is not None and len(selected) >= nnodes_target:
                    break
                if exclusive:
                    if len(free_cores) < len(info["cores"]):
                        continue
                    alloc_cores = frozenset(free_cores)
                    alloc_gpus = frozenset(free_gpus)
                else:
                    need_cores = slots_per_node * slot_size
                    need_gpus = slots_per_node * gpu_per_slot
                    if len(free_cores) < need_cores or len(free_gpus) < need_gpus:
                        continue
                    alloc_cores = frozenset(sorted(free_cores)[:need_cores])
                    alloc_gpus = frozenset(sorted(free_gpus)[:need_gpus])
                selected.append((rank, info, alloc_cores, alloc_gpus))
            if len(selected) < nnodes_min:
                self._check_feasibility(
                    nnodes, nslots, slot_size, exclusive, constraint, gpu_per_slot
                )
                raise InsufficientResources("insufficient resources")
        else:
            nslots_min = nslots
            nslots_target = request.nslots_max  # None → unbounded
            allocated_slots = 0
            for rank, info, free_cores, free_gpus in candidates:
                if nslots_target is not None and allocated_slots >= nslots_target:
                    break
                free_core_slots = len(free_cores) // slot_size
                if gpu_per_slot > 0:
                    free_gpu_slots = len(free_gpus) // gpu_per_slot
                    free_slots = min(free_core_slots, free_gpu_slots)
                else:
                    free_slots = free_core_slots
                if nslots_target is not None:
                    take = min(free_slots, nslots_target - allocated_slots)
                else:
                    take = free_slots  # unbounded: take all available on this node
                if take > 0:
                    ncores_take = take * slot_size
                    ngpus_take = take * gpu_per_slot
                    alloc_cores = frozenset(sorted(free_cores)[:ncores_take])
                    alloc_gpus = frozenset(sorted(free_gpus)[:ngpus_take])
                    selected.append((rank, info, alloc_cores, alloc_gpus))
                    allocated_slots += take
            if allocated_slots < nslots_min:
                self._check_feasibility(
                    nnodes, nslots, slot_size, exclusive, constraint, gpu_per_slot
                )
                raise InsufficientResources("insufficient resources")

        # Compute actual allocated slot count for storage in R.
        if nnodes > 0:
            actual_nslots = slots_per_node * len(selected)
        else:
            actual_nslots = allocated_slots

        # Build result pool and update allocation state on self
        selected_ranks = {rank for rank, _, _, _ in selected}

        result = object.__new__(Rv1Pool)
        result._expiration = 0.0
        result._starttime = 0.0
        result._has_nodelist = True
        result._nslots = actual_nslots
        result._properties = {
            prop: ranks & selected_ranks
            for prop, ranks in self._properties.items()
            if ranks & selected_ranks
        }
        result.scheduling = self.scheduling
        result._ranks = {}
        result._job_state = {}
        result.log = self.log

        for rank, info, alloc_cores, alloc_gpus in selected:
            result._ranks[rank] = {
                "hostname": info["hostname"],
                "cores": alloc_cores,
                "gpus": alloc_gpus,
                "allocated_cores": set(alloc_cores),
                "allocated_gpus": set(alloc_gpus),
                "up": True,
            }
            info["allocated_cores"] |= alloc_cores
            info["allocated_gpus"] |= alloc_gpus

        if request.duration > 0.0:
            end_time = time.time() + request.duration
        elif self._expiration > 0.0:
            end_time = self._expiration
        else:
            end_time = 0.0
        self._job_state[jobid] = (end_time, result)
        self._bump()
        self.log(syslog.LOG_DEBUG, f"alloc: {JobID(jobid).f58}: {result.dumps()}")
        return result

    # ------------------------------------------------------------------
    # ResourcePoolImplementation — structural copies
    # ------------------------------------------------------------------

    def copy(self) -> "Rv1Pool":
        """Return a full independent copy preserving allocation state."""
        new = object.__new__(Rv1Pool)
        new.generation = self.generation
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = getattr(self, "_has_nodelist", False)
        new._properties = {p: set(s) for p, s in self._properties.items()}
        new._ranks = {}
        for rank, info in self._ranks.items():
            new._ranks[rank] = dict(info)
            new._ranks[rank]["allocated_cores"] = set(info["allocated_cores"])
            new._ranks[rank]["allocated_gpus"] = set(info["allocated_gpus"])
        new.scheduling = self.scheduling
        new._job_state = dict(self._job_state)
        # Do not copy self.log: copies are used for simulation (forecast /
        # shadow-time) and their alloc/free calls must not emit log lines.
        return new

    def copy_allocated(self) -> Rv1Set:
        """Return an Rv1Set containing only the allocated resources.

        Properties are intersected with the allocated ranks so that
        queue/property information is preserved (e.g. for sched.resource-status).
        """
        new = object.__new__(Rv1Set)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = False
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "cores": frozenset(info["allocated_cores"]),
                "gpus": frozenset(info["allocated_gpus"]),
            }
            for rank, info in self._ranks.items()
            if info["allocated_cores"] or info["allocated_gpus"]
        }
        allocated_set = set(new._ranks)
        new._properties = {
            prop: ranks & allocated_set
            for prop, ranks in self._properties.items()
            if ranks & allocated_set
        }
        return new

    def copy_down(self) -> Rv1Set:
        """Return an Rv1Set containing only the down ranks."""
        new = object.__new__(Rv1Set)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = False
        new._properties = {}
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "cores": info["cores"],
                "gpus": info["gpus"],
            }
            for rank, info in self._ranks.items()
            if not info["up"]
        }
        return new

    def to_set(self) -> Rv1Set:
        """Return a topology+availability snapshot as an Rv1Set.

        The returned set contains all ranks with their topology (cores,
        gpus) and current up/down state, but no allocation information.
        """
        new = object.__new__(Rv1Set)
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = getattr(self, "_has_nodelist", False)
        new._properties = {p: set(s) for p, s in self._properties.items()}
        new._ranks = {
            rank: {
                "hostname": info["hostname"],
                "cores": info["cores"],
                "gpus": info["gpus"],
            }
            for rank, info in self._ranks.items()
        }
        return new

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _set_allocated(self, other: "Rv1Pool") -> None:
        """Mark resources in *other* as allocated in this pool."""
        for rank, oinfo in other._ranks.items():
            if rank in self._ranks:
                self._ranks[rank]["allocated_cores"] |= oinfo["cores"]
                self._ranks[rank]["allocated_gpus"] |= oinfo["gpus"]

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
                if len(i["cores"]) >= slot_size
                and (gpu_per_slot == 0 or len(i["gpus"]) >= gpu_per_slot)
            ]
            if len(eligible) < need_nodes:
                raise InfeasibleRequest(
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
                if len(i["cores"]) >= cores_per_node and len(i["gpus"]) >= gpus_per_node
            ]
            if len(eligible) < nnodes:
                raise InfeasibleRequest(
                    f"unsatisfiable request: need {nnodes} node(s) with "
                    f"{cores_per_node} cores"
                    + (f" and {gpus_per_node} GPUs" if gpus_per_node else "")
                    + f" each, only {len(eligible)} eligible",
                )
            return

        # A slot must fit within a single rank, so check per-rank slot
        # capacity rather than total cores across all ranks.
        total_slots_avail = sum(len(i["cores"]) // slot_size for _, i in all_candidates)
        if total_slots_avail < nslots:
            total_cores = sum(len(i["cores"]) for _, i in all_candidates)
            raise InfeasibleRequest(
                f"unsatisfiable request: need {nslots} slot(s) of "
                f"{slot_size} core(s), only {total_slots_avail} such "
                f"slot(s) available (total cores: {total_cores})",
            )
        if gpu_per_slot > 0:
            total_gpu_slots = sum(
                min(len(i["cores"]) // slot_size, len(i["gpus"]) // gpu_per_slot)
                for _, i in all_candidates
            )
            if total_gpu_slots < nslots:
                total_gpus = sum(len(i["gpus"]) for _, i in all_candidates)
                raise InfeasibleRequest(
                    f"unsatisfiable request: need {nslots} slot(s) with "
                    f"{gpu_per_slot} GPU(s) each, only {total_gpu_slots} "
                    f"available (total GPUs: {total_gpus})",
                )
