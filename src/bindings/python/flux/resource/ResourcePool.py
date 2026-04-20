###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Public wrapper for Flux scheduler resource pool implementations.

:class:`ResourcePool` is a plain delegating wrapper around a
:class:`~flux.resource.ResourcePoolImplementation.ResourcePoolImplementation`,
following the same pattern as :class:`~flux.resource.ResourceSet.ResourceSet`
wraps a :class:`~flux.resource.ResourceSetImplementation.ResourceSetImplementation`.

Calling ``ResourcePool(R)`` dispatches to the appropriate concrete
implementation based on the ``version`` field in the R JSON:

- Version 1 → :class:`~flux.resource.Rv1Pool.Rv1Pool` (pure-Python)
"""

import json
from collections.abc import Mapping

from flux.resource.ResourcePoolImplementation import (
    InfeasibleRequest,
    InsufficientResources,
    ResourcePoolImplementation,
)


class ResourcePool:
    """Public wrapper for a resource pool implementation.

    Accepts the same argument forms as
    :class:`~flux.resource.ResourceSet.ResourceSet`:

    - An R JSON string or parsed dict → dispatches to the correct
      :class:`ResourcePoolImplementation` subclass by version.
    - A :class:`ResourcePoolImplementation` instance → wraps it directly.
    """

    # Expose the module-level exception classes as class attributes so that
    # callers can use either ResourcePool.InsufficientResources or the
    # canonical flux.resource.InsufficientResources import path.
    InsufficientResources = InsufficientResources
    InfeasibleRequest = InfeasibleRequest

    def __init__(self, arg=None, version=1, log=None, **kwargs):
        """Construct a ResourcePool from an R dict, JSON string, or implementation.

        Args:
            arg: R as a JSON string, parsed dict, a
                :class:`ResourcePoolImplementation` instance, or ``None``.
            version: R version to use when *arg* does not carry one.
            log: Callable for diagnostic messages; receives ``(level, msg)``.
            **kwargs: Forwarded to the implementation constructor.
        """
        if isinstance(arg, ResourcePoolImplementation):
            self.impl = arg
            self.version = getattr(arg, "version", version)
            return

        if isinstance(arg, str):
            arg = json.loads(arg)

        if isinstance(arg, Mapping):
            version = arg.get("version", version)
        elif arg is not None:
            raise TypeError(f"ResourcePool cannot be instantiated from {type(arg)}")

        if version == 1:
            from flux.resource.Rv1Pool import Rv1Pool

            self.impl = Rv1Pool(arg, log=log, **kwargs)
            self.version = 1
        else:
            raise ValueError(f"R version {version} not supported by ResourcePool")

    # ------------------------------------------------------------------
    # Generation counter (pool mutation tracking)
    # ------------------------------------------------------------------

    @property
    def generation(self) -> int:
        """Monotonically increasing mutation counter."""
        return self.impl.generation

    # ------------------------------------------------------------------
    # Availability management
    # ------------------------------------------------------------------

    def mark_up(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as up."""
        self.impl.mark_up(ids)

    def mark_down(self, ids: str) -> None:
        """Mark resources identified by idset string (or ``"all"``) as down."""
        self.impl.mark_down(ids)

    def remove_ranks(self, ranks) -> None:
        """Remove ranks from the pool (called on shrink events)."""
        self.impl.remove_ranks(ranks)

    @property
    def expiration(self) -> float:
        """Resource expiration timestamp (seconds since epoch, 0 = none)."""
        return self.impl.get_expiration()

    @expiration.setter
    def expiration(self, value: float) -> None:
        self.impl.set_expiration(value)

    # ------------------------------------------------------------------
    # Job lifecycle
    # ------------------------------------------------------------------

    def register_alloc(self, jobid: int, R: "ResourcePool") -> None:
        """Register an existing allocation during scheduler reconnect."""
        impl_R = R.impl if isinstance(R, ResourcePool) else R
        self.impl.register_alloc(jobid, impl_R)

    def free(self, jobid: int, R=None, final: bool = False) -> None:
        """Return a job's allocated resources to the pool."""
        r_impl = R.impl if isinstance(R, ResourcePool) else R
        self.impl.free(jobid, r_impl, final)

    def update_expiration(self, jobid: int, expiration: float) -> None:
        """Update the tracked end time for a running job."""
        self.impl.update_expiration(jobid, expiration)

    def job_end_times(self):
        """Return a list of ``(jobid, end_time)`` pairs for all tracked jobs."""
        return self.impl.job_end_times()

    # ------------------------------------------------------------------
    # Scheduling operations
    # ------------------------------------------------------------------

    def parse_resource_request(self, jobspec: dict):
        """Parse a jobspec dict and return a pool-specific resource request."""
        return self.impl.parse_resource_request(jobspec)

    def alloc(self, jobid: int, request) -> "ResourcePool":
        """Allocate resources for *jobid* and return the allocated pool."""
        return ResourcePool(self.impl.alloc(jobid, request))

    def check_feasibility(self, request) -> None:
        """Check whether *request* is structurally satisfiable."""
        self.impl.check_feasibility(request)

    # ------------------------------------------------------------------
    # Structural copies
    # ------------------------------------------------------------------

    def copy(self) -> "ResourcePool":
        """Return a full independent copy preserving allocation state."""
        return ResourcePool(self.impl.copy())

    def to_resource_set(self):
        """Return a topology+availability snapshot as a ResourceSet."""
        from flux.resource.ResourceSet import ResourceSet

        return ResourceSet(self.impl.to_set())

    def copy_allocated(self):
        """Return a ResourceSet containing only the allocated resources."""
        from flux.resource.ResourceSet import ResourceSet

        return ResourceSet(self.impl.copy_allocated())

    def copy_down(self):
        """Return a ResourceSet containing only the down resources."""
        from flux.resource.ResourceSet import ResourceSet

        return ResourceSet(self.impl.copy_down())

    # ------------------------------------------------------------------
    # Serialization and display
    # ------------------------------------------------------------------

    def dumps(self) -> str:
        """Return a compact human-readable summary of the resource set."""
        return self.impl.dumps()

    def to_dict(self) -> dict:
        """Return the resource set as a parsed R JSON dict."""
        return self.impl.to_dict()

    def set_starttime(self, starttime: float) -> None:
        """Set the resource set starttime (seconds since epoch)."""
        self.impl.set_starttime(starttime)

    def set_expiration(self, expiration: float) -> None:
        """Set the resource set expiration (seconds since epoch, 0 = none)."""
        self.impl.set_expiration(expiration)
