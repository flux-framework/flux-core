###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Terminal UI emitter for flux-schedbench.

Drop-in alternative to :class:`flux.testing.events.TestEventEmitter` for
interactive use. Same public method surface (``test_start``, ``stage``,
``progress``, ``result``, ``test_complete``, ``test_error``, ``info``,
``warning``, ``metric``, ``log``) — the benchmark code is identical; the CLI
picks an emitter at startup based on TTY detection and the ``--ui`` flag.

Rendering layout::

flux schedbench · <test_name> <resource summary> <jobspec summary> ·
<scheduler> · <watcher>

<glyph> <stage>  <bar>  <count>  <elapsed>  <rate> ...

<metrics table on completion> <elapsed> elapsed

The block re-renders in place via ANSI cursor moves (cursor-up, then each
line written with an inline erase-to-end-of-line). Redraws throttle to
roughly 20Hz to avoid flicker; a final render is always forced on completion
or error so the latest counts are visible. Each frame is wrapped in DEC
mode 2026 synchronized output so capable terminals batch the update.

Colors are 16-color ANSI only, opt-out via ``NO_COLOR``, ``--color=never``, or
non-TTY ``stream``. No external dependencies.
"""

import os
import sys
import time

from flux.testing.events import _EVENT_MIN_VERBOSITY, QUIET, VERBOSE
from flux.testing.schedbench._ansi import (
    _BAR_CHARS_ASCII,
    _BAR_CHARS_UNICODE,
    _BOLD,
    _CURSOR_HIDE,
    _CURSOR_SHOW,
    _CURSOR_UP_FMT,
    _DIM,
    _ERASE_TO_EOL,
    _FG_CYAN,
    _FG_GREEN,
    _FG_RED,
    _FG_YELLOW,
    _GLYPHS_ASCII,
    _GLYPHS_UNICODE,
    _REDRAW_INTERVAL_S,
    _RESET,
    _SYNC_BEGIN,
    _SYNC_END,
    _color_supported,
    _isatty,
    _safe_glyphs,
)

# Per-status visual style for stage lines: (glyph_color, glyph_key,
# name_color). ``name_color`` of ``None`` means render the stage
# name un-styled. The renderer pulls one row by status rather than
# walking an if/elif chain. Bar color uses a separate table below
# since "done" maps to a different presentation (fully-filled bar
# unconditionally) that doesn't fit the same shape.
_STAGE_STYLES = {
    "done": (_FG_GREEN, "done", None),
    "active": (_FG_YELLOW, "active", _BOLD),
    "failed": (_FG_RED, "failed", _FG_RED),
    "pending": (_DIM, "pending", _DIM),
}

# Bar color by status for the in-progress / pending / failed cases
# (the "done" status is handled inline since it also forces a
# full-width fill regardless of the recorded count).
_BAR_COLORS = {
    "active": _FG_CYAN,
    "failed": _FG_RED,
    "pending": _DIM,
}

# Layout constants. Stage names cap at 7 chars ("cleanup"); count fits
# "1024/1024" = 9; rate fits "5,000 jobs/sec" with room to grow. Bar grows up
# to 40 cells on wide terminals (was 28 — needlessly tight).
_STAGE_NAME_WIDTH = 7
_COUNT_WIDTH = 9
_DEFAULT_WIDTH = 80
_MIN_BAR_WIDTH = 8
_MAX_BAR_WIDTH = 40
_ELAPSED_WIDTH = 6
_RATE_WIDTH = 14


class TerminalEmitter:
    """Render schedbench events as a live multi-line terminal UI.

    Implements the same public methods as
    :class:`flux.testing.events.TestEventEmitter` so the CLI can pick either
    emitter at startup without the benchmark code knowing the difference.

    Args: verbosity: filter threshold; events below the threshold are dropped.
    Defaults to :data:`VERBOSE` since the UI needs ``progress`` and ``metric``
    to display anything interesting. stream: file-like object that receives
    the rendered UI. Defaults to :data:`sys.stdout`. color: ``"auto"``
    (default), ``"always"``, or ``"never"``. ascii_only: force ASCII glyphs
    and bar characters. If omitted, autodetects via locale env vars.
    """

    __test__ = False

    def __init__(self, verbosity=VERBOSE, stream=None, color="auto", ascii_only=None):
        self.verbosity = verbosity
        self._stream = stream if stream is not None else sys.stdout
        self._color = _color_supported(self._stream, color)
        self._ascii = ascii_only if ascii_only is not None else not _safe_glyphs()

        # Cached terminal-capability flags. ``stream`` is captured
        # here and never reassigned, so isatty() at construction
        # time is a stable answer; checking once saves three to
        # four syscalls per redraw.
        self._is_tty = _isatty(self._stream)

        # Glyph and bar-character lookup tables, frozen at init so
        # render paths do dict lookups rather than re-branching on
        # ``self._ascii`` every call.
        self._glyphs = _GLYPHS_ASCII if self._ascii else _GLYPHS_UNICODE
        self._bar_chars = _BAR_CHARS_ASCII if self._ascii else _BAR_CHARS_UNICODE
        # Inline separators used in the header row.
        self._sep = "-" if self._ascii else "·"
        self._times = " x " if self._ascii else " × "

        # Run state.
        self._test_name = None
        self._config = None
        self._stage_names = []
        self._stages = {}  # name → dict of state
        self._current_stage = None
        self._metrics = None
        self._summary_metrics = ()
        self._error = None
        self._start_time = None
        self._end_time = None

        # Rendering state.
        self._last_render_lines = 0
        self._last_render_t = 0.0
        self._cursor_hidden = False

    # ----- Glyph / color helpers (resolve once per draw) -----

    def _c(self, code, text):
        """Wrap ``text`` in ANSI color ``code`` iff color is on."""
        if not self._color:
            return text
        return code + text + _RESET

    # ----- Public API matching TestEventEmitter -----

    def emit(self, name, context=None):
        """Generic event hook. Mostly unused; the typed methods
        below cover the schedbench event names. Kept so consumers that call
        ``emitter.emit("info", ...)`` directly still work."""
        if _EVENT_MIN_VERBOSITY.get(name, QUIET) > self.verbosity:
            return
        # Dispatch to the typed entry points so state updates flow through the
        # same paths regardless of caller style.
        ctx = context or {}
        if name == "test.start":
            self.test_start(
                ctx.get("test_name", "?"),
                ctx.get("stages", []),
                ctx.get("config"),
            )
        elif name == "stage":
            self.stage(
                ctx.get("stage", ""),
                ctx.get("stage_index", 0),
                ctx.get("total_stages", 1),
            )
        elif name == "progress":
            self.progress(
                ctx.get("current", 0),
                ctx.get("total", 0),
                ctx.get("unit", ""),
                ctx.get("rate"),
            )
        elif name == "result":
            self.result(ctx.get("metrics", {}))
        elif name == "test.complete":
            self.test_complete(ctx.get("duration", 0.0))
        elif name == "test.error":
            self.test_error(ctx.get("error", ""))

    def test_start(self, test_name, stages, config=None):
        self._test_name = test_name
        self._stage_names = list(stages)
        self._stages = {
            s: {
                "current": 0,
                "total": 0,
                "unit": "",
                "started": None,
                "finished": None,
                "status": "pending",
            }
            for s in self._stage_names
        }
        self._config = config or {}
        self._start_time = time.time()
        # In quiet mode we render only at test_complete / test_error, so we
        # never paint mid-run and don't need to hide the cursor. _redraw below
        # is a no-op at QUIET.
        if self.verbosity != QUIET and self._is_tty:
            self._stream.write(_CURSOR_HIDE)
            self._cursor_hidden = True
        self._redraw(force=True)

    def stage(self, stage, stage_index, total_stages):
        del stage_index, total_stages  # available in _stage_names
        # Mark previous active stage as done (it transitioned out of being
        # current). The benchmarks emit each stage in order, so anything we
        # entered before this call has finished its work even if its progress
        # count didn't reach total (e.g., the cancel stage in fill-machine has
        # no progress events).
        if self._current_stage is not None:
            prev = self._stages[self._current_stage]
            prev["status"] = "done"
            prev["finished"] = time.time()
        self._current_stage = stage
        if stage in self._stages:
            self._stages[stage]["status"] = "active"
            self._stages[stage]["started"] = time.time()
        self._redraw(force=True)

    def progress(self, current, total, unit, rate=None):
        del rate  # we compute it from elapsed
        if _EVENT_MIN_VERBOSITY["progress"] > self.verbosity:
            return
        if self._current_stage is None:
            return
        st = self._stages[self._current_stage]
        st["current"] = current
        st["total"] = total
        st["unit"] = unit
        self._redraw()

    def result(self, metrics):
        self._metrics = dict(metrics)
        # The final render happens on test_complete/test_error; don't redraw
        # yet (avoids a flash of partial metrics alongside a still-active
        # progress bar).

    def set_summary_metrics(self, summary_metrics):
        """Set the spec for the post-run metrics table.

        The spec is an iterable of ``(key, label, kind)`` triples —
        typically a benchmark's ``SUMMARY_METRICS`` class attribute.
        Each key is looked up in the result dict at render time;
        missing keys are silently skipped. ``kind`` selects a
        formatter in :func:`_format_metric`.

        This is a TerminalEmitter-specific concern: the JSON-event
        emitters (TestEventEmitter, _ResultOnlyEmitter) carry the
        full result dict and don't render a table, so they don't
        implement this method. Callers that want to set the spec
        check ``hasattr(emitter, "set_summary_metrics")`` first.
        """
        self._summary_metrics = tuple(summary_metrics)

    def test_complete(self, duration):
        del duration  # we compute it from _start_time/_end_time
        self._finalize_run("done")

    def test_error(self, error):
        self._error = error
        self._finalize_run("failed")

    def _finalize_run(self, active_stage_status):
        """Shared end-of-run cleanup for both completion and error paths:
        promote any still-active stage to its terminal status, record the
        end time, do the final forced redraw, and restore the cursor.

        ``active_stage_status`` is what to assign to the current stage
        if it's still in the ``"active"`` state. ``test_complete`` passes
        ``"done"``; ``test_error`` passes ``"failed"``.
        """
        if self._current_stage is not None:
            st = self._stages[self._current_stage]
            if st["status"] == "active":
                st["status"] = active_stage_status
                st["finished"] = time.time()
        self._end_time = time.time()
        self._redraw(force=True, final=True)
        self._show_cursor()

    def info(self, message):
        pass  # informational chatter doesn't appear in the UI

    def warning(self, message):
        # Warnings are above-the-UI noise; print to stderr so they don't
        # disrupt the redrawing block on stdout.
        print("warning: " + str(message), file=sys.stderr, flush=True)

    def metric(self, name, value, unit=None):
        pass  # intermediate metrics not displayed

    def log(self, message):
        # Free-form log lines go to stderr (same policy as TestEventEmitter)
        # so they don't disturb the redrawing stdout block.
        print(message, file=sys.stderr, flush=True)

    # ----- Internal: rendering -----

    def _show_cursor(self):
        if self._cursor_hidden and self._is_tty:
            self._stream.write(_CURSOR_SHOW)
            self._stream.flush()
            self._cursor_hidden = False

    def _redraw(self, force=False, final=False):
        # In quiet mode we want exactly one render — the final results block —
        # so suppress every mid-run paint. The final test_complete /
        # test_error call passes final=True to bypass this gate. Nothing else
        # does.
        if self.verbosity == QUIET and not final:
            return

        now = time.time()
        if not force and (now - self._last_render_t) < _REDRAW_INTERVAL_S:
            return
        self._last_render_t = now

        lines = self._render_lines()
        new_count = len(lines)

        # If the new frame has fewer lines than the previous, pad
        # with empties: those padded lines are still emitted with
        # _ERASE_TO_EOL appended below, which scrubs the orphan
        # content from the old frame at those positions.
        if new_count < self._last_render_lines:
            lines = lines + [""] * (self._last_render_lines - new_count)

        # Build the entire frame as a single string and write it
        # in one syscall, so the terminal never sees a partial
        # frame from a fragmented write. Wrap in DEC mode 2026
        # synchronized output (capable terminals batch the update;
        # others ignore the sequence) and rely on per-line
        # _ERASE_TO_EOL so even terminals that don't honour 2026
        # never see a blank-screen state between erase and redraw.
        parts = []
        if self._is_tty:
            parts.append(_SYNC_BEGIN)
            if self._last_render_lines > 0:
                parts.append(_CURSOR_UP_FMT.format(self._last_render_lines))
        parts.append("\n".join(line + _ERASE_TO_EOL for line in lines))
        parts.append("\n")
        if self._is_tty:
            parts.append(_SYNC_END)

        self._stream.write("".join(parts))
        self._stream.flush()
        self._last_render_lines = new_count

    def _term_width(self):
        """Return the actual terminal width by querying the
        stream's controlling TTY directly. We deliberately do not use
        ``shutil.get_terminal_size``, which prefers the ``COLUMNS``
        environment variable — that can be stale when the schedbench command
        runs as a subprocess (e.g., under ``flux start``) inheriting an env
        var the parent shell set for a different window size.
        ``os.get_terminal_size`` on the stream's fd goes straight to
        TIOCGWINSZ and gets the real width.
        """
        try:
            cols = os.get_terminal_size(self._stream.fileno()).columns
        except (AttributeError, ValueError, OSError):
            cols = _DEFAULT_WIDTH
        return max(cols, 40)

    def _render_lines(self):
        """Produce the full block as a list of lines (no
        trailing newlines on individual entries; the writer joins with ``\n``
        and appends one trailing newline)."""
        out = []
        sep = self._sep

        # Header.
        if self._test_name is not None:
            title = (
                self._c(_BOLD, "flux schedbench") + " " + sep + " " + self._test_name
            )
            out.append(title)

        if self._config:
            cfg = self._config
            res_parts = []
            if "nodes" in cfg:
                res_parts.append(_pluralize(cfg["nodes"], "node"))
            if "cores_per_node" in cfg:
                res_parts.append(_pluralize(cfg["cores_per_node"], "core"))
            if cfg.get("gpus_per_node"):
                res_parts.append(_pluralize(cfg["gpus_per_node"], "GPU"))
            misc = []
            if "scheduler" in cfg:
                misc.append(cfg["scheduler"])
            if "watcher" in cfg:
                misc.append(f"{cfg['watcher']} watcher")
            # "real execution" annotates the noteworthy case only; mock is the
            # default and stays unannotated to keep the header tight. The flag
            # also lands in the results JSON and the REAL report column, so
            # this is a glance-level cue rather than the primary record.
            if cfg.get("real_exec"):
                misc.append("real execution")
            res_line = "  " + self._times.join(res_parts)
            if misc:
                res_line += " " + sep + " " + (" " + sep + " ").join(misc)
            out.append(self._c(_DIM, res_line))

        out.append("")

        # Stage lines. In quiet mode the entire stage block is omitted — the
        # user asked not to see progress, and the results summary below
        # carries the per-rate/timing numbers that make the stage block
        # redundant.
        if self.verbosity != QUIET:
            width = self._term_width()
            for name in self._stage_names:
                out.append(self._render_stage_line(name, width))

            out.append("")

        # Metrics (final view only).
        if self._metrics:
            for line in self._render_metrics_lines():
                out.append(line)
            out.append("")

        # Error block.
        if self._error:
            for line in self._error.splitlines():
                out.append("  " + self._c(_FG_RED, line))
            out.append("")

        # Footer: elapsed.
        elapsed = self._elapsed_str()
        if elapsed:
            out.append(self._c(_DIM, "  " + elapsed))

        return out

    def _render_stage_line(self, stage, width):
        st = self._stages[stage]
        status = st["status"]

        glyph_color, glyph_key, name_color = _STAGE_STYLES[status]
        glyph = self._c(glyph_color, self._glyphs[glyph_key])
        name_text = self._c(name_color, stage) if name_color else stage

        # Stage name column. Pad based on display width (color codes don't
        # count toward padding).
        pad = max(0, _STAGE_NAME_WIDTH - len(stage))
        name_col = name_text + (" " * pad)

        # Count and bar.
        current = st["current"]
        total = st["total"]
        count_str = f"{current}/{total}" if total > 0 else ""
        count_col = count_str.rjust(_COUNT_WIDTH)

        # Compute bar width. The fixed columns account for every inter-column
        # 2-space gap plus the four fixed-width columns. The bar grows up to
        # _MAX_BAR_WIDTH on wide terminals. We stop 1 char short of the
        # reported width to avoid the VT100 "phantom column": writing exactly
        # into the rightmost column doesn't wrap until the next character —
        # terminals disagree about whether \n in that state acts as one or two
        # newlines. Staying under the edge sidesteps the question.
        fixed = (
            2  # leading indent
            + 1
            + 1  # glyph + space
            + _STAGE_NAME_WIDTH
            + 2  # gap: name → bar
            + 2  # gap: bar → count
            + _COUNT_WIDTH
            + 2  # gap: count → elapsed
            + _ELAPSED_WIDTH
            + 2  # gap: elapsed → rate
            + _RATE_WIDTH
            + 1  # phantom-column safety margin
        )
        bar_width = max(
            _MIN_BAR_WIDTH,
            min(_MAX_BAR_WIDTH, width - fixed),
        )
        bar_col = self._render_bar(current, total, bar_width, status)

        # Elapsed and rate (only when meaningful). Rate keeps the full unit
        # string from the progress event ("832 job/s") instead of a one-letter
        # abbreviation — the column is wide enough now.
        elapsed_col = ""
        rate_col = ""
        if st["started"] is not None:
            end = st["finished"] if st["finished"] else time.time()
            dt = end - st["started"]
            elapsed_col = f"{dt:5.2f}s"
            if current > 0 and dt > 0 and status in ("active", "done"):
                rate = current / dt
                unit = st["unit"] or ""
                rate_col = f"{rate:5.0f} {unit}/s" if unit else f"{rate:5.0f} /s"

        # Trailing whitespace on empty trailing columns (pending stages have
        # no elapsed/rate) was the second half of the wrap bug — even after
        # fixing the bar-width math, an empty rate column padded to
        # _RATE_WIDTH adds 14 spaces of bloat per pending line. rstrip handles
        # that.
        #
        # The rate column is *left*-justified within _RATE_WIDTH so the
        # numeric portion lines up across stages even when the unit string
        # lengths differ. With rjust, a stage using "job/s" (5 chars) would
        # have its number pushed 3 columns right of a stage using "cancel/s"
        # (8 chars), since both strings right-end at the same column. With
        # ljust, the number's column is fixed by the "{:>5.0f}" prefix and
        # only the unit string extends to the right. Line-level rstrip removes
        # the ljust padding so the phantom-column safety margin still works.
        line = (
            "  "
            + glyph
            + " "
            + name_col
            + "  "
            + bar_col
            + "  "
            + count_col
            + "  "
            + elapsed_col.rjust(_ELAPSED_WIDTH)
            + "  "
            + rate_col.ljust(_RATE_WIDTH)
        )
        return line.rstrip()

    def _render_bar(self, current, total, width, status):
        fill, empty = self._bar_chars
        # A done stage renders a fully-filled bar regardless of the recorded
        # count. Some stages (e.g. fill-machine's cancel stage) emit no
        # progress events, so total stays at 0; showing an empty bar there
        # would suggest nothing happened. "Done" is the source of truth here.
        if status == "done":
            return self._c(_FG_GREEN, fill * width)
        if total <= 0:
            ratio = 0.0
        else:
            ratio = min(1.0, current / total)
        filled = int(round(ratio * width))
        bar = fill * filled + empty * (width - filled)
        return self._c(_BAR_COLORS[status], bar)

    def _render_metrics_lines(self):
        """Format the final metrics table.

        Iterates ``self._summary_metrics`` (set by :meth:`result`) which
        comes from the running benchmark's ``SUMMARY_METRICS`` class
        attribute. Keys absent from the result dict are silently
        skipped, so benchmarks can list metrics that only some runs
        produce. Each row has three columns: label (left-aligned,
        padded to the longest label), number (right-aligned, padded
        to the longest number), unit (dim, left-aligned).
        """
        rows = []
        for key, label, kind in self._summary_metrics:
            if key not in self._metrics:
                continue
            num, unit = _format_metric(self._metrics[key], kind)
            rows.append((label, num, unit))
        if not rows:
            return []

        label_w = max(len(r[0]) for r in rows)
        num_w = max(len(r[1]) for r in rows)
        out = []
        for label, num, unit in rows:
            line = (
                "  "
                + label.ljust(label_w + 2)
                + self._c(_BOLD, num.rjust(num_w))
                + "  "
                + self._c(_DIM, unit)
            )
            out.append(line.rstrip())
        return out

    def _elapsed_str(self):
        if self._start_time is None:
            return ""
        end = self._end_time if self._end_time else time.time()
        dt = end - self._start_time
        sep = " - " if self._ascii else " · "
        suffix = " elapsed"
        if self._end_time and self._error is None:
            suffix += sep + self._c(_FG_GREEN, "ok")
        elif self._end_time and self._error is not None:
            suffix += sep + self._c(_FG_RED, "failed")
        return f"{dt:.2f}s" + suffix


def _format_count(value, unit=None):
    # Label is "jobs"; suppressing the unit avoids a redundant
    # "jobs ... jobs" row in the metrics table.
    return (f"{int(value):,}", "")


def _format_fraction(value, unit=None):
    # A ratio in [0, 1] (e.g. locality score). Three decimals give
    # 0.001 resolution — finer than the 1-in-1000 slots that a
    # typical locality run produces. No unit.
    return (f"{float(value):.3f}", "")


def _format_seconds(value, unit=None):
    return (f"{float(value):.2f}", "s")


def _format_rate(value, unit="job/s"):
    # Rate precision adapts to magnitude: rates >= 1000 don't need
    # decimals, while rates < 100 benefit from two.
    v = float(value)
    if v >= 1000:
        return (f"{v:,.0f}", unit)
    if v >= 100:
        return (f"{v:,.1f}", unit)
    return (f"{v:,.2f}", unit)


#: Dispatch table for :func:`_format_metric`. Adding a new ``kind``
#: is a single entry here — the renderer iterates ``SUMMARY_METRICS``
#: without knowing the available kinds in advance, so benchmarks can
#: reference any kind name listed in this dict.
_METRIC_FORMATTERS = {
    "count": _format_count,
    "fraction": _format_fraction,
    "seconds": _format_seconds,
    "rate": _format_rate,
}


def _format_metric(value, kind):
    """Format ``value`` for display in the metrics table.

    Returns a ``(number, unit)`` tuple so the renderer can
    right-align the number column independently of the unit
    column. ``kind`` may include a unit override separated by
    ``:``, e.g. ``"rate:node/s"`` to display a rate whose natural
    unit is not ``job/s``. Unknown base-kind strings fall back to
    ``str(value)`` with the override unit (or ``""``).
    """
    base_kind = kind
    unit_override = None
    if ":" in kind:
        base_kind, unit_override = kind.split(":", 1)
    formatter = _METRIC_FORMATTERS.get(base_kind)
    if formatter is None:
        return (str(value), unit_override or "")
    if unit_override is not None:
        return formatter(value, unit_override)
    return formatter(value)


def _pluralize(n, label):
    """Format ``"{n} {label}[s]"`` with English singular/plural.
    Picks an "s" suffix for n != 1 (covers 0 and >=2). The pluralization is
    naive; resource labels in this codebase all take a simple -s plural
    (node/nodes, core/cores, GPU/GPUs)."""
    return f"{n} {label}" if n == 1 else f"{n} {label}s"


# vi: ts=4 sw=4 expandtab
