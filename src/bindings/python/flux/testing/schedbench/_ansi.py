###############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Shared ANSI terminal constants and helpers for schedbench UIs.

Both :mod:`flux.testing.schedbench.ui` (single-run UI) and
:mod:`flux.testing.schedbench.sweep` (sweep dashboard) use the same
ANSI escape vocabulary, glyph sets, and terminal-detection helpers.
Centralising them here means a terminal-quirk fix only lands once.
"""

import os

__all__ = [
    "_BAR_CHARS_ASCII",
    "_BAR_CHARS_UNICODE",
    "_BOLD",
    "_CURSOR_HIDE",
    "_CURSOR_SHOW",
    "_CURSOR_UP_FMT",
    "_DIM",
    "_ERASE_TO_EOL",
    "_FG_CYAN",
    "_FG_GREEN",
    "_FG_RED",
    "_FG_YELLOW",
    "_GLYPHS_ASCII",
    "_GLYPHS_UNICODE",
    "_REDRAW_INTERVAL_S",
    "_RESET",
    "_SYNC_BEGIN",
    "_SYNC_END",
    "_color_supported",
    "_isatty",
    "_safe_glyphs",
]

# ANSI escape sequences. One constant per code so readers don't
# need to remember escape numbers.
_ESC = "\x1b["
_RESET = _ESC + "0m"
_BOLD = _ESC + "1m"
_DIM = _ESC + "2m"
_FG_RED = _ESC + "31m"
_FG_GREEN = _ESC + "32m"
_FG_YELLOW = _ESC + "33m"
_FG_CYAN = _ESC + "36m"

# Cursor / line control.
_CURSOR_UP_FMT = _ESC + "{0}A"
# Erase from cursor to end of line. Appended to every rendered line so
# new content scrubs leftover from the previous frame — avoids the
# erase-then-redraw blank flash that older terminals show.
_ERASE_TO_EOL = _ESC + "K"
# Hide / show cursor across the redraw loop.
_CURSOR_HIDE = _ESC + "?25l"
_CURSOR_SHOW = _ESC + "?25h"
# DEC mode 2026 (synchronized output): terminals that recognise this
# batch all writes between BEGIN and END into one screen update.
# iTerm, alacritty, kitty, wezterm, and recent xterm support it;
# rxvt-unicode and older terminals ignore it as an unknown private mode.
_SYNC_BEGIN = _ESC + "?2026h"
_SYNC_END = _ESC + "?2026l"

# Stage status glyphs. Picked to render cleanly on any terminal that
# handles UTF-8; see :func:`_safe_glyphs` for the ASCII fallback path.
_GLYPHS_UNICODE = {
    "pending": "·",
    "active": "▶",
    "done": "✓",
    "failed": "✗",
}
_GLYPHS_ASCII = {
    "pending": ".",
    "active": ">",
    "done": "+",
    "failed": "x",
}

# Progress bar cells. Solid block for completed portion, light shade for
# the remainder.
_BAR_CHARS_UNICODE = ("█", "░")
_BAR_CHARS_ASCII = ("#", "-")

# Minimum delay between repaints (~20 Hz). Fast enough to feel responsive
# without spending the program's whole time budget in render code.
_REDRAW_INTERVAL_S = 0.05


def _isatty(stream):
    try:
        return stream.isatty()
    except (AttributeError, ValueError):
        return False


def _color_supported(stream, color):
    """Resolve ``--color={auto,always,never}`` against the environment.

    Returns True iff colored escape sequences should be emitted on
    ``stream``.
    """
    if color == "never":
        return False
    if color == "always":
        return True
    # auto
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("TERM") == "dumb":
        return False
    return _isatty(stream)


def _safe_glyphs():
    """Return True if the terminal can render the Unicode glyphs we use.

    UTF-8 is the only safe assumption; check the relevant env vars.
    """
    for var in ("LC_ALL", "LC_CTYPE", "LANG"):
        v = os.environ.get(var, "")
        if "UTF-8" in v.upper() or "UTF8" in v.upper():
            return True
    return False


# vi: ts=4 sw=4 expandtab
