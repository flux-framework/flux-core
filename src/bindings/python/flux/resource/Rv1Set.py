###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Pure-Python Rv1 implementation of the ResourceSetImplementation ABC.

:class:`Rv1Set` stores resources as a per-rank dict
``{hostname, cores, gpus}`` and a property mapping
``{name: set-of-ranks}``.  It implements all abstract methods of
:class:`~flux.resource.ResourceSetImplementation.ResourceSetImplementation`
and supports set operations (union, diff, intersect, append, add),
properties, constraints, and round-trip serialization of the RFC 20 Rv1 R
format.
"""

import json
from collections.abc import Mapping
from typing import Dict, List, Set

from flux.hostlist import Hostlist
from flux.idset import IDSET_FLAG_BRACKETS, IDSET_FLAG_RANGE, IDset
from flux.resource.ResourceSetImplementation import ResourceSetImplementation

# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------


def _idset_str(ints) -> str:
    """Return an RFC 22 idset string for the given integers.

    Delegates to :class:`~flux.idset.IDset` so output format matches the
    C library (e.g. ``"0-3"`` rather than ``"[0-3]"``).
    """
    ids = sorted(set(ints))
    if not ids:
        return ""
    return str(IDset(",".join(str(i) for i in ids)))


def _display_idset(ints) -> str:
    """Return a bracketed idset string for human-readable display.

    Single element: ``"3"``; multiple: ``"[0-2]"`` or ``"[0,2,4]"``.
    This matches the C Rlist dumps() format used in test expectations.
    Uses :data:`IDSET_FLAG_BRACKETS` so the C library decides when brackets
    are appropriate.
    """
    ids = sorted(set(ints))
    if not ids:
        return ""
    return IDset(",".join(str(i) for i in ids)).encode(
        flags=IDSET_FLAG_RANGE | IDSET_FLAG_BRACKETS
    )


# ---------------------------------------------------------------------------
# Rv1Set
# ---------------------------------------------------------------------------


class Rv1Set(ResourceSetImplementation):
    """Pure-Python Rv1 ResourceSetImplementation.

    Internal representation:

    - ``_ranks``: mapping from rank integer to
      ``{hostname: str, cores: frozenset, gpus: frozenset}``
    - ``_properties``: mapping from property name to the set of ranks that
      carry that property
    - ``_expiration``, ``_starttime``: resource-set timestamps (float,
      seconds since epoch; 0 = unset)

    Set operations (diff, intersect) operate at core/GPU ID granularity.
    Properties are propagated to the result sets by intersection.

    The constructor accepts any of:

    - ``None`` or no argument → empty set
    - an R JSON string
    - a parsed R JSON dict (``{"version": 1, "execution": {...}}``)
    """

    version = 1

    def __init__(self, arg=None, keep_scheduling: bool = False):
        """Construct from an R JSON string, dict, or ``None`` (empty).

        Args:
            arg: R as a JSON string, parsed dict, or ``None`` for empty.
            keep_scheduling: If True, store the opaque ``R.scheduling`` key
                so it can be propagated to allocations.  Non-scheduler
                components should leave this False (the default) to avoid
                retaining potentially large scheduler-private data.
        """
        self._expiration: float = 0.0
        self._starttime: float = 0.0
        self._has_nodelist: bool = False
        self._ranks: Dict[int, dict] = {}
        self._properties: Dict[str, Set[int]] = {}
        self._scheduling = None

        if arg is None:
            return

        if isinstance(arg, str):
            arg = json.loads(arg)  # may raise json.JSONDecodeError

        if not isinstance(arg, Mapping):
            raise TypeError(f"Rv1Set cannot be instantiated from {type(arg)}")

        version = arg["version"]  # KeyError if missing
        if version != 1:
            raise ValueError(f"R version {version} not supported")

        if keep_scheduling:
            self._scheduling = arg.get("scheduling")

        execution = arg.get("execution", {})
        self._expiration = float(execution.get("expiration") or 0.0)
        self._starttime = float(execution.get("starttime") or 0.0)
        self._nslots: int = int(execution.get("nslots") or 0)

        # Track whether the input included a nodelist so _build_dict can
        # round-trip it faithfully (nodelist is a Flux extension, not in RFC 20).
        raw_nodelist = execution.get("nodelist", [])
        self._has_nodelist: bool = bool(raw_nodelist)

        # Expand hostlist expressions in the nodelist array so we have one
        # hostname per rank in rank order.
        expanded_nodelist: List[str] = []
        for entry in raw_nodelist:
            expanded_nodelist.extend(Hostlist(entry))

        # Collect all (rank, cores, gpus) tuples first so we can assign
        # nodelist entries positionally (nodelist[i] = i-th smallest rank).
        rank_info = []
        for entry in execution.get("R_lite", []):
            rank_ids = IDset(entry["rank"])
            children = entry.get("children", {})
            cores = (
                frozenset(IDset(children["core"]))
                if "core" in children
                else frozenset()
            )
            gpus = (
                frozenset(IDset(children["gpu"])) if "gpu" in children else frozenset()
            )
            for rank in rank_ids:
                rank_info.append((rank, cores, gpus))
        rank_info.sort()
        for pos, (rank, cores, gpus) in enumerate(rank_info):
            hostname = (
                expanded_nodelist[pos]
                if pos < len(expanded_nodelist)
                else f"rank{rank}"
            )
            self._ranks[rank] = {
                "hostname": hostname,
                "cores": cores,
                "gpus": gpus,
            }

        self._properties = {
            prop: set(IDset(ids_str))
            for prop, ids_str in execution.get("properties", {}).items()
        }

    # ------------------------------------------------------------------
    # ResourceSetImplementation abstract method implementations
    # ------------------------------------------------------------------

    def dumps(self) -> str:
        """Return a compact human-readable summary.

        Format: ``rank<idset>/core<idset>[,gpu<idset>][+...]``
        """
        by_resources: Dict[tuple, List[int]] = {}
        for rank, info in self._ranks.items():
            key = (frozenset(info["cores"]), frozenset(info["gpus"]))
            by_resources.setdefault(key, []).append(rank)

        # Sort groups by the smallest rank in each group to match the C
        # rlist_dumps() ordering (rank-ascending), then join with a space
        # separator to match the C library's output format.
        parts = []
        for (cores, gpus), ranks in sorted(
            by_resources.items(), key=lambda item: min(item[1])
        ):
            rank_str = "rank" + _display_idset(ranks)
            core_str = "core" + _display_idset(cores)
            s = f"{rank_str}/{core_str}"
            if gpus:
                s += ",gpu" + _display_idset(gpus)
            parts.append(s)
        return " ".join(parts)

    def encode(self) -> str:
        """Serialize to an R JSON string."""
        return json.dumps(self._build_dict())

    def nodelist(self) -> Hostlist:
        """Return the hostnames in this set as a :class:`~flux.hostlist.Hostlist`."""
        hosts = [info["hostname"] for _, info in sorted(self._ranks.items())]
        return Hostlist(",".join(hosts)) if hosts else Hostlist("")

    def ranks(self) -> IDset:
        """Return the rank integers as a :class:`~flux.idset.IDset`."""
        if not self._ranks:
            return IDset()
        return IDset(",".join(str(r) for r in sorted(self._ranks.keys())))

    def nnodes(self) -> int:
        """Return the number of nodes (ranks) in this set."""
        return len(self._ranks)

    def count(self, name: str) -> int:
        """Return total count of *name* (``"core"`` or ``"gpu"``) across all ranks."""
        if name == "core":
            return sum(len(info["cores"]) for info in self._ranks.values())
        if name == "gpu":
            return sum(len(info["gpus"]) for info in self._ranks.values())
        return 0

    def copy(self) -> "Rv1Set":
        """Return an independent copy."""
        return self._copy_from_ranks(set(self._ranks))

    def copy_ranks(self, ranks) -> "Rv1Set":
        """Return a copy containing only *ranks*."""
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        return self._copy_from_ranks(set(ranks))

    def union(self, other: "Rv1Set") -> "Rv1Set":
        """Return a new Rv1Set with the union of resources from self and *other*.

        For ranks present in both operands, core and GPU ID sets are unioned
        (true ID-level union, mirroring C rlist_union / rnode_union behaviour).
        """
        new = object.__new__(type(self))
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = self._has_nodelist or other._has_nodelist
        new._ranks = {rank: dict(info) for rank, info in self._ranks.items()}
        for rank, info in other._ranks.items():
            if rank not in new._ranks:
                new._ranks[rank] = dict(info)
            else:
                # Union resource IDs for shared ranks (true ID-level union).
                new._ranks[rank]["cores"] = new._ranks[rank]["cores"] | info["cores"]
                new._ranks[rank]["gpus"] = new._ranks[rank]["gpus"] | info["gpus"]
        new._properties = {p: set(s) for p, s in self._properties.items()}
        for prop, ranks in other._properties.items():
            if prop in new._properties:
                new._properties[prop] = new._properties[prop] | ranks
            else:
                new._properties[prop] = set(ranks)
        return new

    def diff(self, other: "Rv1Set") -> "Rv1Set":
        """Return a new Rv1Set with resources in self not in *other*.

        Matching ranks have their core and GPU ID sets reduced by the IDs
        in *other* (mirroring the C rlist_diff / rnode_diff behaviour).
        Ranks with no remaining resources are dropped from the result.
        """
        new = object.__new__(type(self))
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = getattr(self, "_has_nodelist", False)
        new._ranks = {}
        for rank, info in self._ranks.items():
            o = other._ranks.get(rank)
            if o is None:
                new._ranks[rank] = dict(info)
            else:
                cores = info["cores"] - o["cores"]
                gpus = info["gpus"] - o["gpus"]
                if cores or gpus:
                    new._ranks[rank] = {
                        "hostname": info["hostname"],
                        "cores": cores,
                        "gpus": gpus,
                    }
        # Propagate properties restricted to surviving ranks
        rank_set = set(new._ranks)
        new._properties = {
            prop: ranks & rank_set
            for prop, ranks in self._properties.items()
            if ranks & rank_set
        }
        return new

    def intersect(self, other: "Rv1Set") -> "Rv1Set":
        """Return a new Rv1Set with ranks in both self and *other*."""
        return self._copy_from_ranks(set(self._ranks) & set(other._ranks))

    def append(self, other: "Rv1Set") -> None:
        """Add all ranks from *other* into self (mutating).

        For ranks already in self, core and GPU ID sets are unioned so that
        appending disjoint subsets of a rank (e.g. core[0-3] then core[4-7])
        merges them, matching C rlist_append / rnode_add behaviour.
        """
        for rank, info in other._ranks.items():
            if rank not in self._ranks:
                self._ranks[rank] = dict(info)
            else:
                # Merge resource IDs for existing ranks (disjoint resources on
                # the same rank must be unioned, not overwritten).
                self._ranks[rank]["cores"] = self._ranks[rank]["cores"] | info["cores"]
                self._ranks[rank]["gpus"] = self._ranks[rank]["gpus"] | info["gpus"]
        self._has_nodelist = self._has_nodelist or getattr(
            other, "_has_nodelist", False
        )
        for prop, ranks in other._properties.items():
            if prop in self._properties:
                self._properties[prop] |= ranks
            else:
                self._properties[prop] = set(ranks)

    def add(self, other: "Rv1Set") -> None:
        """Add ranks from *other* into self (mutating).

        For ranks not yet in self, the rank is copied from *other*.
        For ranks already in self, the core and GPU ID sets are unioned
        so that merging disjoint subsets of a resource set (e.g. free +
        allocated) reconstructs the full topology.
        """
        new_ranks = set()
        for rank, info in other._ranks.items():
            if rank not in self._ranks:
                self._ranks[rank] = dict(info)
                new_ranks.add(rank)
            else:
                # Union resource IDs for existing ranks so that free +
                # allocated = total (fixes merging partial-rank states).
                self._ranks[rank]["cores"] = self._ranks[rank]["cores"] | info["cores"]
                self._ranks[rank]["gpus"] = self._ranks[rank]["gpus"] | info["gpus"]
        self._has_nodelist = self._has_nodelist or getattr(
            other, "_has_nodelist", False
        )
        for prop, ranks in other._properties.items():
            added = ranks & new_ranks
            if added:
                if prop in self._properties:
                    self._properties[prop] |= added
                else:
                    self._properties[prop] = set(added)

    def remove_ranks(self, ranks) -> "Rv1Set":
        """Remove *ranks* from this set (mutating)."""
        if not isinstance(ranks, IDset):
            ranks = IDset(ranks)
        for rank in ranks:
            self._ranks.pop(rank, None)
            for prop_ranks in self._properties.values():
                prop_ranks.discard(rank)
        return self

    def set_property(self, name: str, ranks=None) -> "Rv1Set":
        """Set property *name* on *ranks* (or all ranks if *ranks* is ``None``)."""
        if "^" in name:
            raise ValueError(
                f"invalid property name {name!r}: '^' is reserved for constraint negation"
            )
        if not name:
            raise ValueError("property name must not be empty")

        if ranks is None:
            target = set(self._ranks.keys())
        else:
            try:
                ids = IDset(str(ranks))
            except Exception as exc:
                raise ValueError(f"invalid rank idset {ranks!r}: {exc}") from exc
            target = set(ids)
            invalid = target - set(self._ranks.keys())
            if invalid:
                raise ValueError(f"ranks {_idset_str(invalid)} not in resource set")

        if name not in self._properties:
            self._properties[name] = set()
        self._properties[name] |= target
        return self

    def get_properties(self) -> str:
        """Return an RFC 20 properties JSON object string."""
        result = {
            prop: _idset_str(sorted(ranks))
            for prop, ranks in self._properties.items()
            if ranks
        }
        return json.dumps(result)

    def get_expiration(self) -> float:
        """Return the resource set expiration (seconds since epoch, 0 = none)."""
        return self._expiration

    def set_expiration(self, expiration: float) -> None:
        """Set the resource set expiration (seconds since epoch, 0 = none)."""
        self._expiration = float(expiration)

    def get_starttime(self) -> float:
        """Return the resource set start time (seconds since epoch, 0 = none)."""
        return self._starttime

    def set_starttime(self, starttime: float) -> None:
        """Set the resource set start time (seconds since epoch)."""
        self._starttime = float(starttime)

    def copy_constraint(self, constraint) -> "Rv1Set":
        """Return a copy containing only ranks that satisfy *constraint*.

        *constraint* may be an RFC 31 constraint dict or a JSON string.
        """
        if isinstance(constraint, str):
            constraint = json.loads(constraint)
        matching = {
            rank
            for rank, info in self._ranks.items()
            if self._matches_constraint(rank, info, constraint)
        }
        return self._copy_from_ranks(matching)

    # ------------------------------------------------------------------
    # Serialization helpers
    # ------------------------------------------------------------------

    def to_dict(self) -> dict:
        """Return the resource set as a parsed R JSON dict."""
        return self._build_dict()

    def _build_dict(self) -> dict:
        """Build the Rv1 R JSON dict from internal state."""
        by_resources: Dict[tuple, List[int]] = {}
        for rank, info in self._ranks.items():
            key = (frozenset(info["cores"]), frozenset(info["gpus"]))
            by_resources.setdefault(key, []).append(rank)

        R_lite = []
        for (cores, gpus), ranks in sorted(
            by_resources.items(), key=lambda item: min(item[1])
        ):
            children: dict = {"core": _idset_str(cores)}
            if gpus:
                children["gpu"] = _idset_str(gpus)
            R_lite.append(
                {
                    "rank": _idset_str(ranks),
                    "children": children,
                }
            )

        execution: dict = {
            "R_lite": R_lite,
            "starttime": self._starttime,
            "expiration": self._expiration,
        }
        if getattr(self, "_nslots", 0) > 0:
            execution["nslots"] = self._nslots

        if getattr(self, "_has_nodelist", False):
            hostnames = [info["hostname"] for _, info in sorted(self._ranks.items())]
            execution["nodelist"] = [str(Hostlist(",".join(hostnames)))]

        if self._properties:
            execution["properties"] = {
                prop: _idset_str(sorted(ranks))
                for prop, ranks in sorted(self._properties.items())
                if ranks
            }

        return {"version": 1, "execution": execution}

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _copy_from_ranks(self, rank_set: set) -> "Rv1Set":
        """Return a new Rv1Set containing only the given ranks."""
        new = object.__new__(type(self))
        new._expiration = self._expiration
        new._starttime = self._starttime
        new._has_nodelist = getattr(self, "_has_nodelist", False)
        new._ranks = {
            rank: dict(self._ranks[rank]) for rank in rank_set if rank in self._ranks
        }
        new._properties = {
            prop: ranks & rank_set
            for prop, ranks in self._properties.items()
            if ranks & rank_set
        }
        return new

    # ------------------------------------------------------------------
    # Constraint matching (internal)
    # ------------------------------------------------------------------

    _KNOWN_CONSTRAINT_OPS = frozenset(
        {"properties", "hostlist", "ranks", "and", "or", "not"}
    )

    def _matches_constraint(self, rank: int, info: dict, constraint: dict) -> bool:
        """Return True if *rank* satisfies the RFC 31 *constraint* expression."""
        if not any(op in constraint for op in self._KNOWN_CONSTRAINT_OPS):
            raise ValueError(
                f"unknown constraint operator(s): {list(constraint.keys())}"
            )

        if "properties" in constraint:
            for prop in constraint["properties"]:
                if not isinstance(prop, str):
                    raise ValueError(
                        f"property constraint must be a string, got {type(prop).__name__}"
                    )
                if prop.startswith("^"):
                    real = prop[1:]
                    if real in self._properties and rank in self._properties[real]:
                        return False
                else:
                    if (
                        prop not in self._properties
                        or rank not in self._properties[prop]
                    ):
                        return False
            return True

        if "hostlist" in constraint:
            return info["hostname"] in Hostlist(constraint["hostlist"])

        if "ranks" in constraint:
            return rank in IDset(constraint["ranks"])

        if "and" in constraint:
            ops = constraint["and"]
            if not isinstance(ops, list):
                raise ValueError(
                    f"'and' operand must be a list, got {type(ops).__name__}"
                )
            return all(self._matches_constraint(rank, info, c) for c in ops)

        if "or" in constraint:
            ops = constraint["or"]
            if not isinstance(ops, list):
                raise ValueError(
                    f"'or' operand must be a list, got {type(ops).__name__}"
                )
            return any(self._matches_constraint(rank, info, c) for c in ops)

        if "not" in constraint:
            ops = constraint["not"]
            if not isinstance(ops, list) or len(ops) < 1:
                raise ValueError(f"'not' operand must be a non-empty list, got {ops!r}")
            return not self._matches_constraint(rank, info, ops[0])

        return True  # unreachable given the known-ops check above
