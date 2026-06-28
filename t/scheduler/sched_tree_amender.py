###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""fake-resources amend-r module: inject TreePool scheduling key.

Topology functions take core and GPU counts from R itself and partition
them evenly across the requested hierarchy.  Reference via the
``module:function`` form of the fake-resources ``amend-r`` config key::

    --conf=fake-resources.amend-r=sched_tree_amender:cluster_a

The directory containing this file must be on PYTHONPATH so importlib can
find it.  Adding a new topology means adding a function that calls
``_inject(R, _make_topo(R, nsockets=..., nnuma_per_socket=...))``.
"""

from flux.idset import IDset


def _all_ranks(R):
    """IDset string covering every rank present in R_lite."""
    result = IDset()
    for entry in R.get("execution", {}).get("R_lite", []):
        result |= IDset(entry["rank"])
    return str(result)


def _make_topo(R, nsockets, nnuma_per_socket=0):
    """Build a TreePool topo dict from R's actual core/GPU counts.

    Cores and GPUs are read from the first R_lite entry and divided
    evenly across the leaf groups implied by the hierarchy.  Raises
    ValueError if the counts don't divide evenly.
    """
    r_lite = R.get("execution", {}).get("R_lite", [])
    children = r_lite[0].get("children", {}) if r_lite else {}
    cores = sorted(IDset(children["core"])) if "core" in children else []
    gpus = sorted(IDset(children["gpu"])) if "gpu" in children else []

    nleaves = nsockets * nnuma_per_socket if nnuma_per_socket else nsockets
    if not cores:
        raise ValueError("R_lite has no cores")
    if len(cores) % nleaves:
        raise ValueError(
            f"{len(cores)} cores not divisible by {nleaves} leaf groups "
            f"({nsockets} socket(s) × {nnuma_per_socket or 1} NUMA)"
        )
    if gpus and len(gpus) % nleaves:
        raise ValueError(
            f"{len(gpus)} GPUs not divisible by {nleaves} leaf groups"
        )

    cpl = len(cores) // nleaves
    gpl = len(gpus) // nleaves if gpus else 0

    def leaf(i):
        node = {"cores": str(IDset(cores[i * cpl:(i + 1) * cpl]))}
        if gpl:
            node["gpus"] = str(IDset(gpus[i * gpl:(i + 1) * gpl]))
        return node

    if nnuma_per_socket:
        return {
            "socket": [
                {"numa": [leaf(s * nnuma_per_socket + n)
                          for n in range(nnuma_per_socket)]}
                for s in range(nsockets)
            ]
        }
    return {"socket": [leaf(s) for s in range(nsockets)]}


def _inject(R, topo):
    R["scheduling"] = {
        "writer": "TreePool",
        "children": [{"ranks": _all_ranks(R), "topo": topo}],
    }
    return R


def cluster_a(R, hwloc_xml=None):
    """Xeon/Nvidia SNC4 (RFC 49 Cluster A): 2 sockets × 4 NUMA."""
    return _inject(R, _make_topo(R, nsockets=2, nnuma_per_socket=4))


def cluster_b(R, hwloc_xml=None):
    """HPE Cray EX MI300A (RFC 49 Cluster B): 4 packages, no NUMA."""
    return _inject(R, _make_topo(R, nsockets=4))


# Default for file-path form of amend-r.
amend = cluster_a
