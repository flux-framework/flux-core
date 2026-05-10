###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Event emission in Flux EventLog (RFC 18) format for tests.

Benchmarks and other tests use :class:`TestEventEmitter` to emit a
JSON-line event stream to stdout. A frontend UI (or any line-
oriented consumer such as ``jq``) reads the stream and renders
progress, stage transitions, and final results without needing to
know test-specific details — the contract is the event names and
the shape of their ``context`` payloads.

**Polarity**: events go to stdout (parseable JSON, exactly one
object per line); logs, warnings, and tracebacks go to stderr.
Mixing prose into stdout breaks parsing. Use :meth:`emitter.log`
for unstructured messages.

**Verbosity**: ``QUIET`` emits only the events a results consumer
needs (``test.start``, ``result``, ``test.complete``, and
``test.error``). ``NORMAL`` adds ``stage`` and ``warning``.
``VERBOSE`` adds ``progress``, ``info``, and ``metric`` for live
UI consumption.
"""

import json
import sys
import time

# Verbosity levels
QUIET = 0
NORMAL = 1
VERBOSE = 2

# Per-event minimum verbosity required for emission. Events whose
# minimum is higher than the emitter's current verbosity are
# silently dropped.
_EVENT_MIN_VERBOSITY = {
    "test.start": QUIET,
    "test.complete": QUIET,
    "test.error": QUIET,
    "result": QUIET,
    "stage": NORMAL,
    "warning": NORMAL,
    "progress": VERBOSE,
    "info": VERBOSE,
    "metric": VERBOSE,
}


class TestEventEmitter:
    """Emit Flux EventLog format events (RFC 18) to stdout.

    Lives in :mod:`flux.testing.events`. The ``Test*`` prefix is
    safe under unittest (which discovers by ``TestCase`` subclassing)
    and the ``__test__ = False`` marker prevents pytest from
    collecting it if pytest is ever used.

    Args:
      verbosity: one of :data:`QUIET`, :data:`NORMAL`,
          :data:`VERBOSE`. Default :data:`NORMAL`.
      stream: file-like object that receives events. Default
          :data:`sys.stdout`. Tests pass a :class:`io.StringIO` to
          capture events.
    """

    __test__ = False

    def __init__(self, verbosity=NORMAL, stream=None):
        self.verbosity = verbosity
        self._stream = stream if stream is not None else sys.stdout

    def emit(self, name, context=None):
        """Emit an event subject to the configured verbosity.

        Events below the configured verbosity threshold are silently
        dropped. The event is serialized as a single JSON line and
        flushed immediately so live consumers can tail the stream.
        """
        if _EVENT_MIN_VERBOSITY.get(name, QUIET) > self.verbosity:
            return
        event = {"timestamp": time.time(), "name": name}
        if context:
            event["context"] = context
        print(json.dumps(event), file=self._stream, flush=True)

    # ----- Required events -----

    def test_start(self, test_name, stages, config=None):
        """Signal test start. Provides UI metadata.

        Args:
          test_name: human-readable test name (e.g. ``"throughput"``).
          stages: list of stage names the test will go through.
          config: optional dict of test configuration, attached for
              results storage and UI display.
        """
        ctx = {"test_name": test_name, "stages": list(stages)}
        if config is not None:
            ctx["config"] = config
        self.emit("test.start", ctx)

    def stage(self, stage, stage_index, total_stages):
        """Signal entering a named stage. Used for progress tracking."""
        self.emit(
            "stage",
            {
                "stage": stage,
                "stage_index": stage_index,
                "total_stages": total_stages,
            },
        )

    def progress(self, current, total, unit, rate=None):
        """Report progress within the current stage.

        Callers control emission frequency (e.g. every 100 items);
        no internal rate-limiting in MVP.
        """
        ctx = {"current": current, "total": total, "unit": unit}
        if rate is not None:
            ctx["rate"] = rate
        self.emit("progress", ctx)

    def result(self, metrics):
        """Emit final test results.

        ``metrics`` is an arbitrary key-value mapping; the UI displays
        keys and values without interpreting them.
        """
        self.emit("result", {"metrics": dict(metrics)})

    def test_complete(self, duration):
        """Signal successful test completion."""
        self.emit("test.complete", {"duration": duration})

    def test_error(self, error):
        """Signal test failure with an error message."""
        self.emit("test.error", {"error": error})

    # ----- Optional events -----

    def info(self, message):
        """Informational message (emitted only at VERBOSE)."""
        self.emit("info", {"message": message})

    def warning(self, message):
        """Non-fatal warning (emitted at NORMAL or VERBOSE)."""
        self.emit("warning", {"message": message})

    def metric(self, name, value, unit=None):
        """Emit an intermediate (non-final) metric.

        Distinct from :meth:`result`, which carries the final
        aggregated metrics. ``metric`` is for live dashboards
        tracking values during execution.
        """
        ctx = {"name": name, "value": value}
        if unit is not None:
            ctx["unit"] = unit
        self.emit("metric", ctx)

    # ----- Unstructured logging (stderr) -----

    def log(self, message):
        """Write an unstructured message to stderr.

        Unlike :meth:`info` (which emits an event to stdout), this
        is for prose log output that should not be parsed by event
        consumers. Always written, regardless of verbosity.
        """
        print(message, file=sys.stderr, flush=True)


# vi: ts=4 sw=4 expandtab
