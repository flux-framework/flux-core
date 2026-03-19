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
from flux.resource.ResourcePoolImplementation import (
    InfeasibleRequest,
    InsufficientResources,
    ResourcePoolImplementation,
)
from flux.resource.Rv1Set import Rv1Set

# Sentinel meaning "nnodes_max / nslots_max not given — same as min (fixed count)".
# Callers that want an unbounded range pass None explicitly.
_NO_MAX = object()


class ResourceRequest:
    """Parsed resource request extracted from a V1 jobspec.

    Returned by :meth:`Rv1Pool.parse_resource_request` and stored on
    :class:`~flux.scheduler.PendingJob`.  Passed to
    :meth:`Rv1Pool.alloc` so that the pool does not need to
    re-parse jobspec on every scheduling pass.

    Attributes:
        nnodes (int): Minimum node count; 0 means any layout.
        nnodes_max (int | None): Maximum node count for RFC 14 range requests.
            Equal to *nnodes* for a fixed count; greater for a bounded range
            ``[nnodes, nnodes_max]``; ``None`` for an unbounded range (take as
            many as are available, subject to *nnodes* minimum).
        nslots (int): Minimum total slot count.
        nslots_max (int | None): Maximum total slot count; same semantics as
            *nnodes_max*.  Used only when *nnodes* is 0 (slot-only layout).
        slot_size (int): Cores per slot.
        gpu_per_slot (int): GPUs per slot.
        duration (float): Walltime in seconds; 0.0 means unlimited.
        constraint: RFC 31 constraint expression (dict) or None.
        exclusive (bool): Whole-node exclusive allocation.
    """

    __slots__ = (
        "nnodes",
        "nnodes_max",
        "nslots",
        "nslots_max",
        "slot_size",
        "gpu_per_slot",
        "duration",
        "constraint",
        "exclusive",
    )

    def __init__(
        self,
        nnodes,
        nslots,
        slot_size,
        gpu_per_slot,
        duration,
        constraint,
        exclusive,
        nnodes_max=_NO_MAX,
        nslots_max=_NO_MAX,
    ):
        self.nnodes = nnodes
        self.nnodes_max = nnodes if nnodes_max is _NO_MAX else nnodes_max
        self.nslots = nslots
        self.nslots_max = nslots if nslots_max is _NO_MAX else nslots_max
        self.slot_size = slot_size
        self.gpu_per_slot = gpu_per_slot
        self.duration = duration
        self.constraint = constraint
        self.exclusive = exclusive

    @staticmethod
    def _parse_count(v, default=1):
        """Parse an RFC 14 count value and return ``(min_val, max_val)``.

        *max_val* equals *min_val* for a fixed count, is greater for a bounded
        range, or is ``None`` for an unbounded range.

        Supported forms (as received from a JSON-parsed jobspec):

        - Integer: ``4`` → ``(4, 4)``
        - Range dict: ``{"min": 2, "max": 8}`` → ``(2, 8)``
        - Unbounded dict: ``{"min": 2}`` → ``(2, None)``
        """
        if isinstance(v, int):
            return v, v
        if isinstance(v, dict):
            operator = v.get("operator", "+")
            operand = int(v.get("operand", 1))
            if operator != "+" or operand != 1:
                raise ValueError(
                    f"RFC 14 range operator={operator!r} operand={operand}"
                    " is not yet supported"
                )
            mn = int(v.get("min", default))
            mx = v.get("max")
            return mn, (int(mx) if mx is not None else None)
        return default, default

    @classmethod
    def from_jobspec(cls, jobspec):
        """Parse a V1 jobspec dict and return a :class:`ResourceRequest`.

        Handles the two common V1 layouts:

        - ``node → slot → core[+gpu]``  (``flux submit -N<n> ...``)
        - ``slot → core[+gpu]``          (``flux submit -n<n> ...``)

        RFC 14 range counts (``{"min": M, "max": N}``) on the
        top-level resource are fully supported: the scheduler allocates as many
        resources as available up to the maximum.

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
            nnodes, nnodes_max = cls._parse_count(top.get("count", 1))
            slot = top["with"][0]
            nslots_per_node, _ = cls._parse_count(slot.get("count", 1))
            nslots = nslots_per_node * nnodes
            # nslots_max scales with nnodes_max; unbounded nnodes → unbounded nslots
            nslots_max = (
                nslots_per_node * nnodes_max if nnodes_max is not None else None
            )
            slot_children = slot["with"]
        elif rtype == "slot":
            nnodes = 0
            nnodes_max = 0  # fixed (slot-only form has no node count)
            nslots, nslots_max = cls._parse_count(top.get("count", 1))
            slot_children = top["with"]
        else:
            raise ValueError(f"unsupported top-level resource type: {rtype!r}")

        slot_size = 1
        gpu_per_slot = 0
        for child in slot_children:
            if child.get("type") == "core":
                slot_size, _ = cls._parse_count(child.get("count", 1))
            elif child.get("type") == "gpu":
                gpu_per_slot, _ = cls._parse_count(child.get("count", 1))

        exclusive = bool(top.get("exclusive", False))
        # Exclusive allocation is per-node; if nnodes was not specified
        # (slot-only jobspec), each slot occupies one exclusive node.
        if exclusive and nnodes == 0:
            nnodes = nslots
            nnodes_max = nslots_max
            nslots_max = nslots  # nslots is fixed; range is expressed via nnodes_max
        # RFC 25: in jobspec V1, attributes.system and duration are required.
        system = jobspec.get("attributes", {}).get("system")
        if jobspec.get("version") == 1:
            if system is None:
                raise ValueError("getting duration: Object item not found: system")
            if system.get("duration") is None:
                raise ValueError("getting duration: Object item not found: duration")
        attrs = system or {}
        duration = attrs.get("duration") or 0.0
        constraint = attrs.get("constraints") or None
        return cls(
            nnodes,
            nslots,
            slot_size,
            gpu_per_slot,
            float(duration),
            constraint,
            exclusive,
            nnodes_max=nnodes_max if nnodes_max != nnodes else _NO_MAX,
            nslots_max=nslots_max if nslots_max != nslots else _NO_MAX,
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

        # Rv1Set.__init__ parses the core resource structure.
        super().__init__(R)

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
                    nnodes_min, nslots, slot_size, exclusive, constraint, gpu_per_slot
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
                    nnodes, nslots_min, slot_size, exclusive, constraint, gpu_per_slot
                )
                raise InsufficientResources("insufficient resources")

        # Compute actual allocated slot count for storage in R.
        if nnodes > 0:
            actual_nslots = slots_per_node * len(selected)
        else:
            actual_nslots = allocated_slots

        # Build result pool and update allocation state on self
        result = object.__new__(Rv1Pool)
        result._expiration = 0.0
        result._starttime = 0.0
        result._has_nodelist = True
        result._nslots = actual_nslots
        result._properties = {}
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
