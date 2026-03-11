###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""C-backed Rv1 resource pool using librlist via CFFI.

Provides :class:`Rv1RlistPool`, a :class:`~flux.resource.pool.ResourcePool`
implementation that delegates resource tracking and allocation to the C
``librlist`` library via :class:`~flux.resource.Rlist.Rlist`.

Compared to :class:`~flux.resource.Rv1Pool.Rv1Pool`:

- Allocation uses the C ``rlist_alloc`` engine, which supports
  ``worst-fit``, ``best-fit``, and ``first-fit`` modes.
- GPU scheduling is not supported; jobs requesting GPUs are denied at
  parse time.

To use this class as the resource pool in a scheduler::

    from flux.resource.Rv1RlistPool import Rv1RlistPool

    class MyScheduler(Scheduler):
        resource_class = Rv1RlistPool
        ...

Or at load time::

    flux module load sched-fifo resource-class=Rv1RlistPool
"""

import json
import time
from collections.abc import Mapping
from typing import Dict, Optional, Tuple

from _flux._rlist import ffi, lib
from flux.resource.ResourcePool import ResourcePool
from flux.resource.Rlist import Rlist


class ResourceRequest:
    """Parsed resource request extracted from a V1 jobspec.

    Returned by :meth:`Rv1RlistPool.parse_resource_request` and stored on
    :class:`~flux.scheduler.PendingJob`.  Passed to :meth:`Rv1RlistPool.alloc`
    so that the pool does not need to re-parse jobspec on every scheduling pass.

    Attributes:
        nnodes (int): Minimum node count; 0 means any layout.
        nslots (int): Total slot count.
        slot_size (int): Cores per slot.
        gpu_per_slot (int): GPUs per slot.
        duration (float): Walltime in seconds; 0.0 means unlimited.
        constraint: RFC 31 constraint expression (dict, JSON string, or None).
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


class Rv1RlistPool(ResourcePool):
    """C-backed Rv1 resource pool using librlist.

    Uses :class:`~flux.resource.Rlist.Rlist` as the backing store.
    Supports worst-fit, best-fit, and first-fit allocation modes but
    does not support GPU scheduling.

    See :class:`~flux.resource.pool.ResourcePool` for the full interface.
    """

    #: GPU allocation is not supported.
    supports_gpu = False

    #: Allocation modes accepted by :meth:`alloc`.
    supported_alloc_modes = frozenset({"worst-fit", "best-fit", "first-fit"})

    def __init__(self, R) -> None:
        """Construct from an R JSON string or dict (Rv1 format)."""
        if isinstance(R, Mapping):
            R = json.dumps(R)
        self._rlist = Rlist(R)
        # jobid -> (end_time, Rv1RlistPool) for all jobs tracked by this pool.
        # Populated by register_alloc() and alloc(); cleared by free().
        self._job_state: Dict[int, Tuple[float, "Rv1RlistPool"]] = {}

    # ------------------------------------------------------------------
    # ResourcePool protocol — resource state management
    # ------------------------------------------------------------------

    def mark_up(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as up."""
        self._rlist.mark_up(ids)
        self._bump()

    def mark_down(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as down."""
        self._rlist.mark_down(ids)
        self._bump()

    @property
    def expiration(self) -> float:
        return self._rlist.expiration

    @expiration.setter
    def expiration(self, value: float) -> None:
        self._rlist.expiration = float(value)

    def remove_ranks(self, ranks) -> None:
        """Remove ranks from the pool (shrink event)."""
        self._rlist.remove_ranks(ranks)
        self._bump()

    def register_alloc(self, jobid: int, R: "Rv1RlistPool") -> None:
        """Register an existing allocation during scheduler reconnect.

        Marks the resources in *R* as allocated and records *jobid* in the
        pool's job state.  Uses ``R.expiration`` as the tracked end time.
        """
        self._rlist.set_allocated(R._rlist)
        self._job_state[jobid] = (R.expiration, R)
        self._bump()

    def free(self, jobid: int) -> None:
        """Return a job's allocated resources to the pool.

        Looks up the allocation by *jobid* in :attr:`_job_state`.
        Does not raise if *jobid* is unknown.
        """
        _, alloc = self._job_state.pop(jobid, (0.0, None))
        if alloc is None:
            return
        self._rlist.free_tolerant(alloc._rlist)
        self._bump()

    # ------------------------------------------------------------------
    # ResourcePool protocol — scheduling operations
    # ------------------------------------------------------------------

    def parse_resource_request(self, jobspec: dict) -> ResourceRequest:
        """Parse a V1 jobspec and return a :class:`ResourceRequest`.

        Raises:
            ValueError: If the jobspec requests GPU resources, which this
                pool does not support.
        """
        rr = ResourceRequest.from_jobspec(jobspec)
        if rr.gpu_per_slot > 0:
            raise ValueError("GPU resources are not supported by Rv1RlistPool")
        return rr

    def alloc(
        self,
        jobid: int,
        request: ResourceRequest,
        mode: Optional[str] = None,
    ) -> "Rv1RlistPool":
        """Allocate resources for *jobid* matching *request*.

        Calls ``rlist_alloc`` from librlist.  If *request.constraint* is set,
        allocation is restricted to a filtered copy of the pool that satisfies
        the RFC 31 constraint.

        Args:
            jobid: The job ID; stored in :attr:`_job_state`.
            request: A :class:`ResourceRequest` from :meth:`parse_resource_request`.
            mode: Allocation mode (``"worst-fit"``, ``"best-fit"``,
                ``"first-fit"``); ``None`` uses the librlist default (worst-fit).

        Returns:
            A new :class:`Rv1RlistPool` containing the allocated resources.

        Raises:
            OSError(ENOSPC): Resources temporarily insufficient.
            OSError: Request infeasible or invalid.
        """
        constraint = request.constraint
        if constraint is not None:
            # Allocate from a constraint-filtered copy, then mark on main pool.
            if not isinstance(constraint, str):
                constraint = json.dumps(constraint)
            filtered = self._rlist.copy_constraint(constraint)
            result_rlist = self._alloc_rlist(filtered, request, mode)
            self._rlist.set_allocated(result_rlist)
        else:
            result_rlist = self._alloc_rlist(self._rlist, request, mode)

        result = self._wrap(result_rlist)
        if request.duration > 0.0:
            end_time = time.time() + request.duration
        elif self._rlist.expiration > 0.0:
            end_time = self._rlist.expiration
        else:
            end_time = 0.0
        self._job_state[jobid] = (end_time, result)
        self._bump()
        return result

    def check_feasibility(self, request: ResourceRequest) -> None:
        """Check whether *request* is structurally satisfiable.

        Allocates from a clean copy of the pool (all resources up and
        unallocated) to determine whether the request could ever be satisfied,
        regardless of current availability.

        Raises:
            OSError: If the request cannot be satisfied even when all resources
                are available.
        """
        test = self.copy()
        test.mark_up("all")
        test.alloc(0, request)

    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the tracked end time for a running job."""
        if jobid in self._job_state:
            _, alloc = self._job_state[jobid]
            self._job_state[jobid] = (expiration, alloc)
            self._bump()

    # ------------------------------------------------------------------
    # ResourcePool protocol — structural copies
    # ------------------------------------------------------------------

    def deepcopy(self) -> "Rv1RlistPool":
        """Return a full copy of the pool preserving allocation state.

        Clones the underlying rlist (including which resources are marked
        allocated) and reconstructs :attr:`_job_state` with independent
        copies of each job's allocated resource set.  This allows the pool
        to be used in forward simulations without mutating the live state.
        """
        new = Rv1RlistPool._wrap(self._rlist.clone())
        new.generation = self.generation
        new._rlist.expiration = self._rlist.expiration
        for jobid, (end_time, alloc) in self._job_state.items():
            alloc_copy = Rv1RlistPool._wrap(alloc._rlist.clone())
            new._job_state[jobid] = (end_time, alloc_copy)
        return new

    def copy(self) -> "Rv1RlistPool":
        """Return a structural copy with allocation state cleared."""
        return self._wrap(self._rlist.copy())

    def copy_allocated(self) -> "Rv1RlistPool":
        """Return a copy containing only the allocated resources."""
        return self._wrap(self._rlist.copy_allocated())

    def copy_down(self) -> "Rv1RlistPool":
        """Return a copy containing only the down resources."""
        return self._wrap(self._rlist.copy_down())

    # ------------------------------------------------------------------
    # ResourcePool protocol — serialization and display
    # ------------------------------------------------------------------

    def to_dict(self) -> dict:
        """Return the resource set as a parsed R JSON dict."""
        return json.loads(self._rlist.encode())

    def dumps(self) -> str:
        """Return a compact human-readable summary of the resource set."""
        return self._rlist.dumps()

    def set_starttime(self, starttime: float) -> None:
        """Set the resource set starttime (seconds since epoch)."""
        self._rlist.set_starttime(starttime)

    def set_expiration(self, expiration: float) -> None:
        """Set the resource set expiration (seconds since epoch, 0 = none)."""
        self._rlist.set_expiration(expiration)

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _alloc_rlist(
        rlist: Rlist,
        request: ResourceRequest,
        mode: Optional[str],
    ) -> Rlist:
        """Call ``rlist_alloc`` on *rlist* and return the allocated Rlist."""
        mode_buf = ffi.new("char[]", mode.encode("utf-8")) if mode else ffi.NULL
        ai = ffi.new("struct rlist_alloc_info *")
        ai.nnodes = request.nnodes
        ai.nslots = request.nslots
        ai.slot_size = request.slot_size
        ai.exclusive = request.exclusive
        ai.mode = mode_buf
        ai.constraints = ffi.NULL
        error = ffi.new("flux_error_t *")
        result = lib.rlist_alloc(rlist.handle, ai, error)
        if result == ffi.NULL:
            err = ffi.errno
            msg = ffi.string(error.text).decode("utf-8")
            raise OSError(err, msg if msg else "insufficient resources")
        return Rlist(handle=result)

    @staticmethod
    def _wrap(rlist: Rlist) -> "Rv1RlistPool":
        """Wrap an Rlist in a new Rv1RlistPool with empty job state."""
        obj = object.__new__(Rv1RlistPool)
        obj._rlist = rlist
        obj._job_state = {}
        return obj
