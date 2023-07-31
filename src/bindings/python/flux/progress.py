###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import atexit
import datetime
import shutil
import sys
import time

# Bottombar heavily borrows from
#
#  https://github.com/evalf/bottombar/blob/master/bottombar.py
#
# Copyright (c) 2020 Evalf
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.


class ElapsedTime(float):
    """
    An ElapsedTime object is a floating point elapsed time in seconds
    that comes with a convenient "dt" property that returns a
    datetime.timedelta object
    """

    @property
    # pylint: disable=invalid-name
    def dt(self):
        return datetime.timedelta(seconds=round(self))


class Bottombar:
    """Maintain a status line at bottom of terminal using vt100 escape codes

    The Bottombar class implements a very simple status line which stays
    positioned at the last line of vt100 capable terminals through the
    use of vt100 escape codes.

    This class will only work properly on vt100 compatible terminals, which
    includes xterm, rxvt, and gnome-terminal and their derivatives on Linux,
    as well as iTerm and Terminal on OSX, and reportedly the new Windows
    Terminal on Windows.

    Use of Bottombar requires that a ``formatter`` function be provided.
    The ``formatter`` will be called on each update as::

        formatter(bbar, width)

    Where ``bbar`` is the bottombar object being formatted and ``width`` is
    the current terminal width at the time of the update. The default
    ``formatter`` will simply print all extra

    As a convenience, the Bottombar constructor collects all extra keyword
    arguments and presents them as attributes on the Bottombar object for
    later access from within and outside the provided ``formatter``, e.g::

        def formatter(bb, width):
            text = f"iteration={bb.i}"
            return text + time.ctime().rjust(width - len(text))

        bb = Bottombar(formatter, i=0).start()
        for i in range(0, 128):
            bb.update(i=i)
            time.sleep(.05)
        bb.stop()

    will print a statusbar with an iteration count left justified, and the
    current time right justified.

    Attributes:
        elapsed (float): The elapsed time since ``bb.start()`` in floating
            point seconds. As a convenience, ``bb.elapsed`` may be converted
            to a ``datetime.timedelta`` object via the ``dt`` attribute,
            e.g. ``bb.elapsed.dt``.

    Args:
        formatter (function): Function which returns the status string
        kwargs: all extra keyword arguments are collected in the Bottombar
            instance and made available as attributes for convenience

    """

    def __init__(self, formatter=None, **kwargs):
        self.size = None
        if formatter is None:
            formatter = self._format
        self.formatter = formatter
        self.kwargs = kwargs
        self._running = False
        self._t0 = None

    def __getattr__(self, attr):
        if attr == "elapsed":
            return ElapsedTime(time.time() - self._t0)
        return self.kwargs[attr]

    def _format(self, _bbar, _width):
        return " ".join([f"{key}={val}" for key, val in self.kwargs.items()])

    def __str__(self):
        return self.formatter(self, self.size.columns)

    def _setup_terminal(self, size=None):
        """
        Reset terminal scroll region and save last line for progressbar
        """
        if size:
            self.size = size
        sys.stdout.write(
            "\0337"  # save cursor and attributes
            "\033[r"  # reset scroll region (moves cursor)
            "\0338"  # restore cursor and attributes
            "\033D"  # move/scroll down
            "\033M"  # move up
            "\0337"  # save cursor and attributes
            "\033[1;%dr"  # set scroll region to lines - 1
            "\0338" % (self.size.lines - 1)  # restore cursor and attributes
        )

    def _reset_terminal(self):
        """Reset terminal after use of progress bar

        Reset terminal scroll region, print final version of progress bar.
        Print newline.
        """
        sys.stdout.write(
            "\0337"  # save cursor position
            "\033[%d;1H"  # move cursor to bottom row, first column
            "\033[K"  # clear entire line
            "\033[r"  # reset scroll region
            "\0338"  # restore cursor position
            "%s\n" % (self.size.lines, self)  # print final bar
        )
        atexit.unregister(self._reset_terminal)

    def redraw(self):
        """Redraw bar without update"""
        size = shutil.get_terminal_size()
        if self.size != size:
            self._setup_terminal(size)
        sys.stdout.write(
            "\0337"  # save cursor and attributes
            "\033[%d;1H"  # move cursor to bottom row, first column
            "\033[2K"  # clear entire line
            "\033[?7l"  # disable line wrap
            "\033[0m%s"  # clear attributes, print bar
            "\033[?7h"  # enable line wrap
            "\0338" % (self.size.lines, self)
        )
        sys.stdout.flush()

    def start(self):
        """Start drawing a Bottombar"""
        self._running = True
        if self._t0 is None:
            self._t0 = time.time()
        self.redraw()
        atexit.register(self._reset_terminal)
        return self

    def stop(self):
        """Reset terminal and write final bottombar state with newline"""
        if self._running:
            self._reset_terminal()
            self._running = False

    def update(self, **kwargs):
        """Update keyword args and redraw a bottombar"""
        self.kwargs.update(kwargs)
        if self._running:
            self.redraw()


class ProgressBar(Bottombar):
    """Simple progress bar that stays on last line of terminal

    The ProgressBar class uses the features of Bottombar to create a
    progress bar, plus optional other text, which stays on the last line
    of a terminal. A vt100 compatible terminal is required.

    Args:
        total (int): The total expected number of items/units for which
                     the progressbar is monitoring progress, default=100.
        style (str, optional): A string progress bar style from the list
                               "line", "bar", "dots", "steps", "vertbars".
        before (str, optional): A string to place before the progress bar.
        after  (str, optional): A string to place after the progress bar.
                                default=" {percent:5.1f}%"
        autostop (bool, optional): If True, ProgressBar instance will be
                                   automatically stopped when count == total.
                                   Otherwise, terminal reset will be deferred
                                   to an atexit handler.
        kwargs (optional): Extra keyword args are saved and passed as args
                           when formatting the ``before`` and ``after``
                           strings.

    The ``before`` and ``after`` strings are formatted on each update to
    the progressbar and passed all extra keyword args, plus the current
    ``total``, ``count``, ``percent``, and ``elapsed`` time e.g.::

        before_str = before.format(
                        total=total,
                        count=count,
                        percent=percent,
                        elapsed=elapsed,
                        **kwargs
                     )

    which means that these strings are most useful when they are format
    strings, e.g.::

        ProgressBar(before="Running {total} jobs, {percent}% complete: ")

    """

    bar_style = {
        "line": "─━",
        "bar": "─█",
        "dots": "⣀⣄⣤⣦⣶⣷⣿",
        "steps": " ▁▂▃▄▅▆▇█",
        "vertbars": " ▏▎▍▌▋▊▉█",
    }

    def __init__(
        self,
        total=100,
        style="vertbars",
        before="",
        after=" {percent:5.1f}%",
        autostop=False,
        **kwargs,
    ):
        super().__init__(self._formatter, **kwargs)
        self.count = 0
        self.total = total
        self.before = before
        self.after = after
        self.style = self.bar_style[style]
        self.autostop = autostop

    def __getattr__(self, attr):
        if attr == "total":
            return self.total
        if attr == "count":
            return self.count
        if attr == "elapsed":
            return super().__getattr__(attr)
        return self.kwargs[attr]

    def _formatter(self, bbar, width):
        style = self.style
        fraction = float(self.count / self.total) if self.total else 0
        percent = 100 * fraction

        #  Format before/after strings:
        before = self.before.format(
            total=self.total,
            count=self.count,
            elapsed=self.elapsed,
            percent=percent,
            **bbar.kwargs,
        )
        after = self.after.format(
            total=self.total,
            count=self.count,
            elapsed=self.elapsed,
            percent=percent,
            **bbar.kwargs,
        )

        #  Calculate remaining length for progress bar:
        length = width - len(before) - len(after) - 2

        #  Calculate amount of bar fully filled and build string
        fraction = min(fraction, 1.0)
        fill = int(length * fraction)
        filled = style[-1] * fill

        #  Now calculate partial fill and append it to filled block
        part = int(length * len(style) * fraction) - fill * len(style)
        if part:
            fill += 1
            filled += style[part]

        #  Build the bar string as 'filled':
        filled = "\u2502" + filled + style[0] * (length - fill) + "\u2502"
        return f"{before}{filled}{after}"

    def update(self, advance=1, **kwargs):
        """Update the state of a ProgressBar

        Update the state of a ProgressBar and redraw if the progress bar is
        currently running. If ``count == total`` and autostop is set, the
        progress bar will be automatically stopped.

        When the progress bar is stopped, the terminal will be reset and
        the final state of the bar will be left on the last line of output.

        Args:
            advance (int, optional): Advance progress by ``advance`` amount.
            kwargs: Update stored keyword arguments
        """
        self.count += advance
        super().update(**kwargs)
        if self.count == self.total and self.autostop:
            self.stop()
