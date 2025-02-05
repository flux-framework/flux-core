###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import json
import sys
from datetime import datetime, timedelta


class EventLogEvent(dict):
    """
    wrapper class for a single KVS EventLog entry
    """

    def __init__(self, event):
        """
        "Initialize from a string or dict eventlog event
        """
        if isinstance(event, str):
            event = json.loads(event)
        super().__init__(event)
        if "context" not in self:
            self["context"] = {}

    def __str__(self):
        return "{0.timestamp:<0.5f}: {0.name} {0.context}".format(self)

    @property
    def name(self):
        return self["name"]

    @property
    def timestamp(self):
        return self["timestamp"]

    @property
    def context(self):
        return self["context"]

    @property
    def context_string(self):
        if not self.context:
            return ""
        return json.dumps(
            self.context, ensure_ascii=False, separators=(",", ":"), sort_keys=True
        )


class EventLogFormatter:
    """Formatter for eventlog event entries

    This class is used by utilities to format eventlog entries in
    with optional formatting such as colorization, human-readable and
    offset timestamps, etc.
    """

    BOLD = "\033[1m"
    YELLOW = "\033[33m"
    GREEN = "\033[32m"
    BLUE = "\033[34m"
    MAGENTA = "\033[35m"
    GRAY = "\033[37m"
    RED = "\033[31m"

    eventlog_colors = {
        "name": YELLOW,
        "time": GREEN,
        "timebreak": BOLD + GREEN,
        "key": BLUE,
        "value": MAGENTA,
        "number": GRAY,
        "exception": BOLD + RED,
    }

    def __init__(self, format="text", timestamp_format="raw", color="auto"):
        """Initialize an eventlog formatter

        Args:
            format (str): The format to use for the eventlog entry. Valid
                values include "text" (default), or "json".
            timestamp_format (str): The format of the timestamp. Valid values
                include "raw" (default), "iso", "offset", "human"
            color (str): When to use color. Valid values include "auto" (the
                default, "always" or "never".
        """

        self.t0 = None
        self.last_dt = None
        self.last_ts = None

        if format not in ("text", "json"):
            raise ValueError(f"Invalid entry_fmt: {format}")
        self.entry_format = format

        if timestamp_format not in ("raw", "human", "reltime", "iso", "offset"):
            raise ValueError(f"Invalid timestamp_fmt: {timestamp_format}")
        if timestamp_format == "reltime":
            timestamp_format = "human"
        self.timestamp_format = timestamp_format

        if color not in ("always", "never", "auto"):
            raise ValueError(f"Invalid color: {color}")
        if color == "always":
            self.color = True
        elif color == "never":
            self.color = False
        elif color == "auto":
            self.color = sys.stdout.isatty()

    def _color(self, name):
        if self.color:
            return self.eventlog_colors[name]
        return ""

    def _reset(self):
        if self.color:
            return "\033[0m"
        return ""

    def _timestamp_human(self, ts):
        dt = datetime.fromtimestamp(ts)

        if self.last_dt is not None:
            delta = dt - self.last_dt
            if delta < timedelta(minutes=1):
                sec = delta.total_seconds()
                return self._color("time") + f"[{sec:>+11.6f}]" + self._reset()
        # New minute. Save last timestamp, print dt
        self.last_ts = ts
        self.last_dt = dt

        mday = dt.astimezone().strftime("%b%d %H:%M")
        return self._color("timebreak") + f"[{mday}]" + self._reset()

    def _timestamp(self, ts):
        if self.t0 is None:
            self.t0 = ts

        if self.timestamp_format == "human":
            return self._timestamp_human(ts)

        if self.timestamp_format == "raw":
            result = f"{ts:11.6f}"
        elif self.timestamp_format == "iso":
            dt = datetime.fromtimestamp(ts).astimezone()
            tz = dt.strftime("%z").replace("+0000", "Z")
            us = int((ts - int(ts)) * 1e6)
            result = dt.astimezone().strftime("%Y-%m-%dT%T.") + f"{us:06d}" + tz
        else:  # offset
            ts -= self.t0
            result = f"{ts:15.6f}"

        return self._color("time") + result + self._reset()

    def _format_text(self, entry):
        ts = self._timestamp(entry.timestamp)
        if entry.name == "exception":
            name = self._color("exception") + entry.name + self._reset()
        else:
            name = self._color("name") + entry.name + self._reset()
        context = ""
        for key, val in entry.context.items():
            key = self._color("key") + key + self._reset()
            if type(val) in (int, float):
                color = self._color("number")
            else:
                color = self._color("value")
            val = color + json.dumps(val, separators=(",", ":")) + self._reset()
            context += f" {key}={val}"

        return f"{ts} {name}{context}"

    def _format_json(self, entry):
        # remove context if it is empty
        if "context" in entry and not entry["context"]:
            entry = {k: v for k, v in entry.items() if k != "context"}
        return json.dumps(entry, separators=(",", ":"))

    def format(self, entry):
        """Format an eventlog entry
        Args:
            entry (:obj:`dict` or :obj:`EventLogEntry`): The entry to format.
                If a :obj:`dict`, then the entry must conform to RFC 18.

        Returns:
            str: The formatted eventlog entry as a string.
        """

        if not isinstance(entry, EventLogEvent):
            entry = EventLogEvent(entry)
        if self.entry_format == "json":
            return self._format_json(entry)
        return self._format_text(entry)
