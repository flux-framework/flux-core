###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""flux.testing.schedbench: scheduler benchmarking infrastructure.

Public API is re-exported from the submodules so callers can write
``from flux.testing.schedbench import ThroughputBenchmark`` rather
than walking the full module path.
"""

from flux.testing.schedbench.benchmarks import (
    BENCHMARKS,
    Benchmark,
    FillMachineBenchmark,
    ThroughputBenchmark,
    simple_jobspec,
)
from flux.testing.schedbench.locality import (
    LocalityBenchmark,
    LocalityPredicate,
)
from flux.testing.schedbench.results import BenchmarkResults
from flux.testing.schedbench.ui import TerminalEmitter

# Benchmark subclasses auto-register in BENCHMARKS via
# Benchmark.__init_subclass__, so importing a module that defines
# one is enough — no explicit registration step.
#
# Third-party plugins (not currently in use) can register without
# touching flux-core via setuptools entry points in the group
# ``flux.testing.schedbench.benchmarks``. To enable, append after
# the in-tree imports above:
#
#     from importlib.metadata import entry_points
#     for ep in entry_points(group="flux.testing.schedbench.benchmarks"):
#         try:
#             ep.load()  # importing triggers __init_subclass__
#         except Exception as exc:
#             import warnings
#             warnings.warn(f"schedbench plugin {ep.name!r}: {exc}")
#
# Plugin authors then declare in their pyproject.toml:
#
#     [project.entry-points."flux.testing.schedbench.benchmarks"]
#     my-bench = "my_pkg.my_module"

__all__ = (
    "BENCHMARKS",
    "Benchmark",
    "BenchmarkResults",
    "FillMachineBenchmark",
    "LocalityBenchmark",
    "LocalityPredicate",
    "TerminalEmitter",
    "ThroughputBenchmark",
    "simple_jobspec",
)

# vi: ts=4 sw=4 expandtab
