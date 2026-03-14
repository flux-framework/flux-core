###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Abstract base class for Flux scheduler resource pool implementations.

A resource pool manages the set of resources available to a scheduler and
tracks which resources are currently allocated.  Pool implementations must
inherit from :class:`ResourcePool` and implement all abstract methods.

The :class:`~flux.scheduler.Scheduler` base class interacts with the pool
exclusively through this interface; scheduler subclasses call :meth:`alloc`,
:meth:`free`, :meth:`check_feasibility`, and :meth:`update_expiration` directly.
"""

from abc import ABC, abstractmethod
from typing import Optional


class ResourcePool(ABC):
    """Abstract base class for resource pool implementations.

    Subclasses must implement all abstract methods.  The constructor must
    accept a single argument: an R JSON string or a parsed R dict.

    Pool instances serve double duty: the main pool tracks all resources and
    running-job state, while objects returned by :meth:`alloc`,
    :meth:`copy`, :meth:`copy_allocated`, and :meth:`copy_down` are
    lightweight views used for serialization and inspection.

    The :attr:`generation` counter is incremented by :meth:`_bump` after
    every mutation (alloc, free, mark_up/down, update_expiration, etc.).
    Callers can snapshot ``pool.generation`` before a :meth:`deepcopy` and
    compare later to detect whether the copy has gone stale.
    """

    #: Monotonically increasing counter; incremented by every mutation.
    #: Class-level default of 0 is shadowed by an instance attribute on the
    #: first :meth:`_bump` call, so no explicit ``__init__`` is required.
    generation: int = 0

    def _bump(self) -> None:
        """Increment :attr:`generation` to signal that pool state has changed."""
        self.generation += 1

    # ------------------------------------------------------------------
    # Resource state management (called by Scheduler base class)
    # ------------------------------------------------------------------

    @abstractmethod
    def mark_up(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as up."""

    @abstractmethod
    def mark_down(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as down."""

    @property
    @abstractmethod
    def expiration(self) -> float:
        """Resource set expiration timestamp (seconds since epoch, 0 = none).

        Subclasses must also provide a setter so that the base class can write
        ``self.resources.expiration = value``.
        """

    @abstractmethod
    def remove_ranks(self, ranks) -> None:
        """Remove ranks from the pool (called on shrink events)."""

    @abstractmethod
    def register_alloc(self, jobid: int, R: "ResourcePool") -> None:
        """Register an existing allocation during scheduler reconnect.

        Marks the resources in *R* as allocated and records *jobid* in the
        pool's job state.  *R* is an instance of the same pool class
        constructed from the job's R record; its expiration is used as the
        tracked end time.
        """

    @abstractmethod
    def free(self, jobid: int) -> None:
        """Return a job's allocated resources to the pool.

        Looks up the allocation by *jobid* and returns its resources.
        Must not raise if *jobid* is unknown.
        """

    # ------------------------------------------------------------------
    # Scheduling operations (called by Scheduler subclasses)
    # ------------------------------------------------------------------

    @abstractmethod
    def parse_resource_request(self, jobspec: dict):
        """Parse a jobspec dict and return a pool-specific resource request.

        The returned object is opaque to the base class and is stored on
        :class:`~flux.scheduler.PendingJob` for later passing to
        :meth:`alloc`.

        Raises:
            ValueError: If the jobspec cannot be parsed or requests resources
                the pool does not support (e.g. GPUs on a CPU-only pool).
            KeyError: If required jobspec fields are missing.
        """

    @abstractmethod
    def alloc(self, jobid: int, request, mode: Optional[str] = None) -> "ResourcePool":
        """Allocate resources for *jobid* matching *request*.

        Args:
            jobid: The job ID; stored in the pool's job state.
            request: A resource request returned by :meth:`parse_resource_request`.
            mode: Allocation strategy (e.g. ``"worst-fit"``); pool-specific.

        Returns:
            A new pool instance representing the allocated resources.

        Raises:
            OSError(ENOSPC): Resources temporarily insufficient.
            OSError(EOVERFLOW): Request structurally infeasible.
        """

    @abstractmethod
    def check_feasibility(self, request) -> None:
        """Check whether *request* is structurally satisfiable.

        Tests against a clean copy of the pool (all resources up and
        unallocated) so that current availability does not affect the result.

        Raises:
            OSError: If the request cannot be satisfied even when all resources
                are available.
        """

    @abstractmethod
    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the tracked end time for a running job.

        Called when job-manager issues a ``sched.expiration`` update.

        Args:
            jobid: The job ID.
            expiration: New expiration timestamp (seconds since epoch).
        """

    # ------------------------------------------------------------------
    # Structural copies (called by Scheduler base class for resource-status)
    # ------------------------------------------------------------------

    @abstractmethod
    def deepcopy(self) -> "ResourcePool":
        """Return a full independent copy of the pool preserving allocation state.

        Required so that schedulers can snapshot resource state for forward
        simulation without mutating the live pool.  Mutations to the copy
        (alloc, free, update_expiration) must not affect the original.
        """

    @abstractmethod
    def copy(self) -> "ResourcePool":
        """Return a structural copy of the pool with allocation state cleared."""

    @abstractmethod
    def copy_allocated(self) -> "ResourcePool":
        """Return a copy containing only the allocated resources."""

    @abstractmethod
    def copy_down(self) -> "ResourcePool":
        """Return a copy containing only the down (not schedulable) resources."""

    # ------------------------------------------------------------------
    # Serialization and display (called by Scheduler base class and subclasses)
    # ------------------------------------------------------------------

    @abstractmethod
    def to_dict(self) -> dict:
        """Return the resource set as a parsed R JSON dict."""

    @abstractmethod
    def dumps(self) -> str:
        """Return a compact human-readable summary of the resource set."""

    @abstractmethod
    def set_starttime(self, starttime: float) -> None:
        """Set the resource set starttime (seconds since epoch)."""

    @abstractmethod
    def set_expiration(self, expiration: float) -> None:
        """Set the resource set expiration (seconds since epoch, 0 = none)."""
