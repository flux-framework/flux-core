###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Append-only JSON results storage for flux-schedbench.

:class:`BenchmarkResults` is a thin persistence layer for the
per-run records the CLI accumulates. It stores aggregate metrics
only (no per-job data) so files stay small (~1-2 KB per run) and
machine-readable. The CLI builds a complete run record dict with
all metadata and benchmark results, calls :meth:`add_run`, then
:meth:`save`; the JSON file is written atomically via tmp+rename.

The file format is ``{"runs": [<record>, ...]}`` — a single top-
level object with a ``runs`` array, one entry per benchmark
invocation. The record shape is defined in the design doc's
"Results Storage" section.
"""

import json
import os
import time


class BenchmarkResults:
    """Append-only JSON store for benchmark run records.

    Args:
      path: filesystem path to the results file. The file is
          created on first :meth:`save` if it does not yet exist;
          if it exists, its contents are loaded on construction.

    Usage::

        results = BenchmarkResults("schedbench-results.json")
        results.add_run({"test_name": "throughput", ...})
        results.save()
    """

    def __init__(self, path):
        self.path = path
        self.runs = self._load()

    def add_run(self, run):
        """Append a run record to the in-memory list.

        ``timestamp`` (Unix seconds) and ``iso_timestamp`` (UTC
        ``YYYY-MM-DDTHH:MM:SSZ``) are added if not already present.
        The record is not persisted until :meth:`save` is called.

        Returns the appended record (with timestamp fields filled).
        """
        run = dict(run)
        run.setdefault("timestamp", time.time())
        run.setdefault(
            "iso_timestamp",
            time.strftime(
                "%Y-%m-%dT%H:%M:%SZ",
                time.gmtime(run["timestamp"]),
            ),
        )
        self.runs.append(run)
        return run

    def get_runs(self):
        """Return a shallow copy of the in-memory runs list."""
        return list(self.runs)

    def save(self):
        """Persist runs to disk atomically.

        Writes to ``<path>.tmp`` then renames over the existing file.
        :func:`os.replace` is atomic on POSIX and Windows, so a
        partial write cannot leave the file in a half-formed state.
        """
        tmp = self.path + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"runs": self.runs}, f, indent=2)
            f.write("\n")
        # os.replace is atomic on POSIX only when tmp and self.path
        # are on the same filesystem. Both live in the same directory
        # by construction (tmp = self.path + ".tmp"), so this holds.
        os.replace(tmp, self.path)

    def _load(self):
        """Load existing runs from disk, or return [] if the file
        does not yet exist."""
        try:
            with open(self.path) as f:
                data = json.load(f)
        except FileNotFoundError:
            return []
        return data.get("runs", [])


# vi: ts=4 sw=4 expandtab
