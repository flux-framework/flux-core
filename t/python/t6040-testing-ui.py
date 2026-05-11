###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Unit tests for :class:`flux.testing.schedbench.ui.TerminalEmitter`.

Pure-Python tests: the emitter writes to a :class:`io.StringIO` rather than a
real terminal, so no broker is required and the rendered output can be
inspected directly.

Tests assert on substring presence rather than exact byte layouts so future
column-width tuning doesn't break the suite. ANSI escape sequences are
stripped from the captured output before substring checks unless the test
specifically verifies color behavior.
"""

import io
import os
import re
import sys
import unittest

# Required to set PYTHONPATH for the in-tree layout sharness runs us from. We
# don't call rerun_under_flux (these tests don't need a broker), but the
# import itself must still run.
import subflux  # noqa: F401
from flux.testing.events import NORMAL, VERBOSE
from flux.testing.schedbench.ui import (
    TerminalEmitter,
    _color_supported,
    _pluralize,
)

# Strip ANSI CSI sequences from a string. Used to verify rendered content
# without depending on color/cursor escape bytes.
_ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def _strip_ansi(s):
    return _ANSI_RE.sub("", s)


def _last_frame(stream):
    """Split the captured stream into rendered frames (separated
    by cursor-up + erase-to-end-of-screen sequences) and return the last one
    with ANSI codes stripped. This is what the user would see at the end of
    the run."""
    raw = stream.getvalue()
    frames = re.split(r"\x1b\[\d+A\x1b\[J", raw)
    return _strip_ansi(frames[-1])


class TestPluralize(unittest.TestCase):

    def test_singular(self):
        self.assertEqual(_pluralize(1, "node"), "1 node")
        self.assertEqual(_pluralize(1, "GPU"), "1 GPU")

    def test_plural_for_zero_and_many(self):
        self.assertEqual(_pluralize(0, "node"), "0 nodes")
        self.assertEqual(_pluralize(2, "core"), "2 cores")
        self.assertEqual(_pluralize(64, "core"), "64 cores")


class TestColorSupported(unittest.TestCase):
    """Color resolution from --color and the environment."""

    def setUp(self):
        # Snapshot env so per-test mutations don't leak. The _color_supported
        # function reads NO_COLOR and TERM.
        self._saved_env = {k: os.environ.get(k) for k in ("NO_COLOR", "TERM")}

    def tearDown(self):
        for k, v in self._saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v

    def test_never_disables_regardless_of_tty(self):
        os.environ.pop("NO_COLOR", None)
        self.assertFalse(_color_supported(sys.stdout, "never"))

    def test_always_enables_regardless_of_tty(self):
        os.environ["NO_COLOR"] = "1"  # would normally disable
        self.assertTrue(_color_supported(io.StringIO(), "always"))

    def test_auto_disabled_by_no_color_env(self):
        os.environ["NO_COLOR"] = "1"
        # Even a "real" tty stream should give False under NO_COLOR.
        self.assertFalse(_color_supported(sys.stdout, "auto"))

    def test_auto_disabled_by_dumb_term(self):
        os.environ.pop("NO_COLOR", None)
        os.environ["TERM"] = "dumb"
        self.assertFalse(_color_supported(sys.stdout, "auto"))

    def test_auto_disabled_on_non_tty(self):
        os.environ.pop("NO_COLOR", None)
        os.environ.pop("TERM", None)
        # StringIO is not a tty; isatty() returns False.
        self.assertFalse(_color_supported(io.StringIO(), "auto"))


#: Default summary spec installed by ``_BaseEmitterTest.setUp``.
#: Covers the metric keys exercised by the rendering tests below;
#: tests that need a different spec (e.g. to exercise the
#: ``fraction`` kind) call ``self.emitter.set_summary_metrics(...)``
#: themselves to override.
_DEFAULT_SUMMARY_SPEC = (
    ("njobs", "jobs", "count"),
    ("throughput", "throughput", "rate"),
    ("submit_rate", "submit rate", "rate"),
    ("alloc_rate", "alloc rate", "rate"),
)


class _BaseEmitterTest(unittest.TestCase):
    """Shared setup: a StringIO stream, color disabled, ASCII
    safe-mode disabled so we exercise the Unicode glyphs."""

    def setUp(self):
        self.stream = io.StringIO()
        self.emitter = TerminalEmitter(
            verbosity=VERBOSE,
            stream=self.stream,
            color="never",
            ascii_only=False,
        )
        self.emitter.set_summary_metrics(_DEFAULT_SUMMARY_SPEC)


class TestTestStart(_BaseEmitterTest):

    def test_renders_test_name_in_header(self):
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={"nodes": 4, "cores_per_node": 64},
        )
        frame = _last_frame(self.stream)
        self.assertIn("flux schedbench", frame)
        self.assertIn("throughput", frame)

    def test_renders_resource_summary(self):
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={
                "nodes": 4,
                "cores_per_node": 64,
                "gpus_per_node": 8,
                "scheduler": "sched-simple",
                "watcher": "journal",
            },
        )
        frame = _last_frame(self.stream)
        self.assertIn("4 nodes", frame)
        self.assertIn("64 cores", frame)
        self.assertIn("8 GPUs", frame)
        self.assertIn("sched-simple", frame)
        self.assertIn("journal watcher", frame)

    def test_omits_gpus_when_zero(self):
        """gpus_per_node=0 means the run is cores-only; the
        header shouldn't read '0 GPUs'."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={"nodes": 4, "cores_per_node": 8, "gpus_per_node": 0},
        )
        frame = _last_frame(self.stream)
        self.assertNotIn("GPU", frame)

    def test_real_exec_annotates_header(self):
        """real_exec=True appends 'real execution' after the
        watcher, so a glance at the header tells the user this run actually
        ran jobs rather than simulating them. The annotation lives next to
        watcher because both are broker-mode descriptors — not a top-level
        field like node count."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={
                "nodes": 4,
                "cores_per_node": 64,
                "gpus_per_node": 0,
                "scheduler": "sched-simple",
                "watcher": "journal",
                "real_exec": True,
            },
        )
        frame = _last_frame(self.stream)
        self.assertIn("real execution", frame)

    def test_mock_exec_unannotated(self):
        """real_exec=False (default mode) leaves the header
        unannotated — mock is the common case and a 'mock execution' tag would
        clutter every run without adding signal."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={
                "nodes": 4,
                "cores_per_node": 64,
                "gpus_per_node": 0,
                "scheduler": "sched-simple",
                "watcher": "journal",
                "real_exec": False,
            },
        )
        frame = _last_frame(self.stream)
        self.assertNotIn("real execution", frame)
        self.assertNotIn("mock execution", frame)

    def test_renders_all_stages_as_pending(self):
        """Before any stage event, every stage should appear with
        the pending glyph."""
        self.emitter.test_start(
            "fill-machine",
            stages=["fill", "cancel", "cleanup"],
            config={"nodes": 2, "cores_per_node": 2},
        )
        frame = _last_frame(self.stream)
        self.assertIn("fill", frame)
        self.assertIn("cancel", frame)
        self.assertIn("cleanup", frame)
        # Pending glyph (the centered dot) appears for each stage.
        self.assertGreaterEqual(frame.count("·"), 3)


class TestStageTransitions(_BaseEmitterTest):

    def test_stage_marks_active_glyph(self):
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        frame = _last_frame(self.stream)
        # Active glyph (right-pointing triangle) on the current row.
        self.assertIn("▶", frame)

    def test_previous_stage_marked_done_on_next_stage(self):
        """When stage() transitions to the next stage, the
        previous one should switch from active to done."""
        self.emitter.test_start(
            "fill-machine",
            stages=["fill", "cancel", "cleanup"],
            config={},
        )
        self.emitter.stage("fill", 0, 3)
        self.emitter.stage("cancel", 1, 3)
        frame = _last_frame(self.stream)
        # Done glyph for fill, active for cancel.
        self.assertIn("✓", frame)
        # Active glyph for cancel.
        self.assertIn("▶", frame)


class TestProgress(_BaseEmitterTest):

    def test_progress_updates_count(self):
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        self.emitter.progress(512, 1024, "job")
        # Force a final render (default throttle would skip the second
        # progress call).
        self.emitter._redraw(force=True)
        frame = _last_frame(self.stream)
        self.assertIn("512/1024", frame)

    def test_progress_ignored_below_verbosity(self):
        """At NORMAL verbosity, progress events are dropped (the UI is
        informationally degraded but still renders stage transitions).
        """
        e = TerminalEmitter(
            verbosity=NORMAL,
            stream=self.stream,
            color="never",
            ascii_only=False,
        )
        e.test_start("throughput", stages=["execute"], config={})
        e.stage("execute", 0, 1)
        e.progress(512, 1024, "job")
        e._redraw(force=True)
        frame = _last_frame(self.stream)
        # Count should not appear since the progress event was below
        # threshold.
        self.assertNotIn("512/1024", frame)


class TestCompletion(_BaseEmitterTest):

    def test_metrics_rendered_on_complete(self):
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        self.emitter.progress(1024, 1024, "job")
        self.emitter.result(
            {
                "throughput": 753.6,
                "submit_rate": 832.5,
                "njobs": 1024,
            }
        )
        self.emitter.test_complete(duration=2.4)
        frame = _last_frame(self.stream)
        # Labels (human-readable, not the raw key names).
        self.assertIn("throughput", frame)
        self.assertIn("submit rate", frame)
        self.assertIn("jobs", frame)
        # Values formatted with adaptive rate precision: 832.5 is in the
        # 100-999 range so it gets 1 decimal place.
        self.assertIn("832.5", frame)
        self.assertIn("753.6", frame)
        # Integer count uses thousands separator.
        self.assertIn("1,024", frame)
        # Units appear alongside numbers.
        self.assertIn("job/s", frame)
        # Footer shows ok status.
        self.assertIn("ok", frame)

    def test_raw_timestamps_filtered_out(self):
        """``t_*`` keys are dropped from the displayed table; the
        UI shows only curated derived metrics. The raw timestamps are still
        preserved in the results JSON file for analysis — just not in the
        on-screen summary."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        self.emitter.result(
            {
                "throughput": 700.0,
                "t_submit_start": 1778509870.41,
                "t_all_submitted": 1778509872.49,
                "t_first_submit_event": 1778509870.63,
                "t_last_submit_event": 1778509871.32,
            }
        )
        self.emitter.test_complete(duration=2.4)
        frame = _last_frame(self.stream)
        # The curated metric is shown.
        self.assertIn("throughput", frame)
        self.assertIn("700", frame)
        # The raw timestamp labels do not appear — neither the key names nor
        # the unformatted epoch values.
        self.assertNotIn("t_submit_start", frame)
        self.assertNotIn("t_first_submit_event", frame)
        self.assertNotIn("1,778,509,870", frame)
        self.assertNotIn("1778509870", frame)

    def test_adaptive_rate_precision(self):
        """Rate formatting adjusts decimals to magnitude: ≥1000
        no decimals, ≥100 one decimal, otherwise two decimals. Keeps the
        column readable across HPC-scale ranges."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        self.emitter.result(
            {
                "submit_rate": 5234.7,  # ≥1000 → "5,235"
                "alloc_rate": 234.56,  # ≥100  → "234.6"
                "throughput": 12.345,  # <100  → "12.35"
            }
        )
        self.emitter.test_complete(duration=1.0)
        frame = _last_frame(self.stream)
        self.assertIn("5,235", frame)
        self.assertIn("234.6", frame)
        self.assertIn("12.35", frame)

    def test_active_stage_finalized_to_done(self):
        """If test_complete fires while a stage is still active
        (no explicit final progress event reaching total), the stage
        transitions to done so the bar appears full."""
        self.emitter.test_start(
            "throughput",
            stages=["execute"],
            config={},
        )
        self.emitter.stage("execute", 0, 1)
        self.emitter.progress(500, 1024, "job")  # not yet at total
        self.emitter.result({"throughput": 100.0})
        self.emitter.test_complete(duration=1.0)
        frame = _last_frame(self.stream)
        # Done glyph appears even though count never hit total.
        self.assertIn("✓", frame)


class TestErrorPath(_BaseEmitterTest):

    def test_error_displays_diagnostic(self):
        self.emitter.test_start(
            "fill-machine",
            stages=["fill", "cancel", "cleanup"],
            config={},
        )
        self.emitter.stage("fill", 0, 3)
        diag = (
            "fill-machine: no job reached 'start' event\n"
            "  jobs: 4 requested, 4 submitted, 0 submit-failed\n"
            "  0/4 reached event 'alloc'"
        )
        self.emitter.test_error(diag)
        frame = _last_frame(self.stream)
        # Failed glyph on the fill row, diagnostic text below.
        self.assertIn("✗", frame)
        self.assertIn("no job reached", frame)
        self.assertIn("4 requested", frame)
        # Footer shows failed status.
        self.assertIn("failed", frame)


class TestStreamPolarity(_BaseEmitterTest):
    """log() and warning() must go to stderr, not the UI stream
    on stdout — otherwise prose breaks the redrawing block."""

    def test_warning_writes_to_stderr_not_ui_stream(self):
        # Redirect stderr so we can inspect what was written.
        saved_stderr = sys.stderr
        sys.stderr = io.StringIO()
        try:
            self.emitter.warning("disk space low")
            self.assertEqual(self.stream.getvalue(), "")
            self.assertIn("disk space low", sys.stderr.getvalue())
        finally:
            sys.stderr = saved_stderr

    def test_log_writes_to_stderr_not_ui_stream(self):
        saved_stderr = sys.stderr
        sys.stderr = io.StringIO()
        try:
            self.emitter.log("some prose")
            self.assertEqual(self.stream.getvalue(), "")
            self.assertIn("some prose", sys.stderr.getvalue())
        finally:
            sys.stderr = saved_stderr


class TestColorAndAscii(unittest.TestCase):

    def test_color_never_omits_escape_codes(self):
        """No ANSI sequences should appear in the output stream
        when color is explicitly disabled."""
        stream = io.StringIO()
        e = TerminalEmitter(
            verbosity=VERBOSE,
            stream=stream,
            color="never",
            ascii_only=False,
        )
        e.test_start("throughput", stages=["execute"], config={"nodes": 4})
        e.stage("execute", 0, 1)
        e.test_complete(duration=0.1)
        # Cursor-control sequences ARE present (they're how the block
        # redraws); colors are not. Filter to SGR (style) sequences which end
        # in 'm'.
        raw = stream.getvalue()
        sgr = re.findall(r"\x1b\[[0-9;]*m", raw)
        self.assertEqual(sgr, [])

    def test_color_always_emits_escape_codes(self):
        stream = io.StringIO()
        e = TerminalEmitter(
            verbosity=VERBOSE,
            stream=stream,
            color="always",
            ascii_only=False,
        )
        e.test_start("throughput", stages=["execute"], config={})
        raw = stream.getvalue()
        sgr = re.findall(r"\x1b\[[0-9;]*m", raw)
        self.assertGreater(len(sgr), 0)

    def test_ascii_mode_uses_safe_glyphs(self):
        stream = io.StringIO()
        e = TerminalEmitter(
            verbosity=VERBOSE,
            stream=stream,
            color="never",
            ascii_only=True,
        )
        e.test_start("throughput", stages=["execute"], config={})
        e.stage("execute", 0, 1)
        e.progress(50, 100, "job")
        e._redraw(force=True)
        e.test_complete(duration=1.0)
        frame = _last_frame(stream)
        # Unicode glyphs should be absent under ASCII mode.
        for g in ("·", "▶", "✓", "✗", "█", "░"):
            self.assertNotIn(g, frame)
        # Done glyph in ASCII mode is "+".
        self.assertIn("+", frame)


class TestRedrawThrottling(unittest.TestCase):
    """progress() calls in quick succession must not redraw on
    every call — the cost would dominate at scale (thousands of job events). A
    50ms throttle is sufficient."""

    def test_unforced_redraws_throttle(self):
        stream = io.StringIO()
        e = TerminalEmitter(
            verbosity=VERBOSE,
            stream=stream,
            color="never",
            ascii_only=False,
        )
        e.test_start("throughput", stages=["execute"], config={})
        e.stage("execute", 0, 1)
        # Reset render timestamp so the very first throttled progress call
        # lands in the throttle window.
        e._last_render_t = e._last_render_t or 0
        # Hammer with 50 progress events; they should not all produce frames
        # (the throttle kicks in for unforced redraws).
        before = stream.getvalue().count("execute")
        for i in range(50):
            e.progress(i, 100, "job")
        after = stream.getvalue().count("execute")
        # Each frame contains one "execute" occurrence. We expect FAR fewer
        # than 50 frames given the 50ms throttle and the negligible cost of
        # the loop.
        self.assertLess(after - before, 50)


if __name__ == "__main__":
    # Emit TAP output so sharness can parse per-test pass/fail results. Unlike
    # t6000/t6010/t6030, this file's tests are pure-Python (no broker
    # required) so rerun_under_flux is not needed.
    from pycotap import TAPTestRunner

    unittest.main(testRunner=TAPTestRunner(), buffer=False)


# vi: ts=4 sw=4 expandtab
