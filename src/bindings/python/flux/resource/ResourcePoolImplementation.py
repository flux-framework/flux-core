###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Abstract base class for resource pool implementations.

:class:`ResourcePoolImplementation` extends
:class:`~flux.resource.ResourceSetImplementation.ResourceSetImplementation`
with the scheduler-layer protocol: availability tracking (up/down),
allocation, and per-job state management.
"""

from abc import abstractmethod

from flux.resource.ResourceSetImplementation import ResourceSetImplementation


class InsufficientResources(OSError):
    """Not enough resources available right now — retry after a free event."""


class InfeasibleRequest(OSError):
    """Request can never be satisfied by this pool."""


class ResourcePoolImplementation(ResourceSetImplementation):  # pragma: no cover
    """ABC for resource pool implementations.

    Extends :class:`ResourceSetImplementation` with availability tracking
    and allocation management required by the scheduler layer.

    The :attr:`generation` counter is incremented by :meth:`_bump` after
    every mutation (alloc, free, mark_up/down, update_expiration, etc.).
    Callers can snapshot ``pool.generation`` before a :meth:`copy` and
    compare later to detect whether the copy has gone stale.
    """

    #: Monotonically increasing counter; incremented by every mutation.
    #: Class-level default shadowed by an instance attribute on first bump.
    generation: int = 0

    def log(self, level: int, msg: str) -> None:
        """Log a message at syslog priority *level*.

        Replaced at construction time by a :class:`~flux.brokermod.BrokerLogger`
        instance (or any callable with the same ``(level, msg)`` signature) so
        that pool implementations can call ``self.log(level, msg)``
        unconditionally.  This no-op default is used when no logger is supplied
        (e.g. in unit tests).
        """

    def _bump(self) -> None:
        """Increment :attr:`generation` to signal that pool state has changed."""
        self.generation += 1

    @property
    def expiration(self) -> float:
        """Resource expiration timestamp (seconds since epoch, 0 = none)."""
        return self.get_expiration()

    @expiration.setter
    def expiration(self, value: float) -> None:
        self.set_expiration(value)

    # ------------------------------------------------------------------
    # Availability management
    # ------------------------------------------------------------------

    @abstractmethod
    def mark_up(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as up."""
        raise NotImplementedError

    @abstractmethod
    def mark_down(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as down."""
        raise NotImplementedError

    @abstractmethod
    def copy_down(self):
        """Return a resource set containing only the down (not schedulable) resources."""
        raise NotImplementedError

    # ------------------------------------------------------------------
    # Job lifecycle
    # ------------------------------------------------------------------

    @abstractmethod
    def register_alloc(self, jobid: int, R) -> None:
        """Register an existing allocation during scheduler reconnect."""
        raise NotImplementedError

    @abstractmethod
    def free(self, jobid: int, R=None, final: bool = False) -> None:
        """Return a job's allocated resources to the pool."""
        raise NotImplementedError

    @abstractmethod
    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the tracked end time for a running job."""
        raise NotImplementedError

    @abstractmethod
    def job_end_times(self):
        """Return a list of ``(jobid, end_time)`` pairs for all tracked jobs."""
        raise NotImplementedError

    # ------------------------------------------------------------------
    # Scheduling operations
    # ------------------------------------------------------------------

    @abstractmethod
    def parse_resource_request(self, jobspec: dict):
        """Parse a jobspec dict and return a pool-specific resource request."""
        raise NotImplementedError

    @abstractmethod
    def alloc(self, jobid: int, request):
        """Allocate resources for *jobid* matching *request*."""
        raise NotImplementedError

    @abstractmethod
    def check_feasibility(self, request) -> None:
        """Check whether *request* is structurally satisfiable."""
        raise NotImplementedError

    # ------------------------------------------------------------------
    # Structural copies
    # ------------------------------------------------------------------

    @abstractmethod
    def copy(self):
        """Return a full independent copy preserving allocation state."""
        raise NotImplementedError

    @abstractmethod
    def copy_allocated(self):
        """Return a resource set containing only the allocated resources."""
        raise NotImplementedError

    @abstractmethod
    def to_set(self):
        """Return a topology+availability snapshot as a resource set."""
        raise NotImplementedError
