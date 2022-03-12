###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import re
import sys
import errno
import json
import logging
import os
import math
import argparse
import traceback
import signal
import threading
import shutil
from datetime import datetime, timedelta
from string import Formatter
from collections import namedtuple

from flux.core.inner import ffi, raw
from flux.utils.parsedatetime import Calendar

__all__ = [
    "check_future_error",
    "encode_payload",
    "encode_topic",
    "CLIMain",
    "parse_fsd",
]


def check_future_error(func):
    def func_wrapper(calling_obj, *args, **kwargs):
        try:
            return func(calling_obj, *args, **kwargs)
        except EnvironmentError as error:
            try:
                future = (
                    calling_obj.handle
                    if hasattr(calling_obj, "handle")
                    else calling_obj
                )
                errmsg = raw.flux_future_error_string(future)
            except EnvironmentError:
                raise error from None
            if errmsg is None:
                raise error from None
            raise EnvironmentError(error.errno, errmsg.decode("utf-8")) from None

    return func_wrapper


def interruptible(func):
    """Make a Future method interruptible via Ctrl-C

    Necessary for Future methods that may block when calling into the C API.
    """

    def func_wrapper(future, *args, **kwargs):
        # python only allows `signal.signal` calls in the main thread
        # only activate if the process is not in the Flux reactor
        active = False
        flux_handle = future.get_flux()
        if (
            threading.current_thread() is threading.main_thread()
            and flux_handle is not None
            and not flux_handle.reactor_running()
        ):
            handler = signal.getsignal(signal.SIGINT)
            signal.signal(signal.SIGINT, signal.SIG_DFL)
            active = True
        retval = func(future, *args, **kwargs)
        if active:
            signal.signal(signal.SIGINT, handler)
        return retval

    return func_wrapper


def encode_payload(payload):
    if payload is None or payload == ffi.NULL:
        payload = ffi.NULL
    elif isinstance(payload, str):
        payload = payload.encode("UTF-8", errors="surrogateescape")
    elif not isinstance(payload, bytes):
        payload = json.dumps(payload, ensure_ascii=False).encode(
            "UTF-8", errors="surrogateescape"
        )
    return payload


def encode_topic(topic):
    # Convert topic to utf-8 binary string
    if topic is None or topic == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "Topic must not be None/NULL")
    if isinstance(topic, str):
        topic = topic.encode("UTF-8")
    elif not isinstance(topic, bytes):
        errmsg = "Topic must be a string, not {}".format(type(topic))
        raise TypeError(errno.EINVAL, errmsg)
    return topic


def help_formatter(argwidth=40):
    """
    Return our 'clean' HelpFormatter, if possible, with a wider default
     for the max width allowed for options.
    """

    required = ("_format_action_invocation", "_metavar_formatter", "_format_args")
    if not all(hasattr(argparse.HelpFormatter, name) for name in required):
        logging.getLogger(__name__).warning(
            "required argparse methods missing, falling back to HelpFormatter."
        )
        return lambda prog: argparse.HelpFormatter(prog, max_help_position=argwidth)

    class FluxHelpFormatter(argparse.HelpFormatter):
        def _format_action_invocation(self, action):
            if not action.option_strings:
                (metavar,) = self._metavar_formatter(action, action.dest)(1)
                return metavar

            opts = list(action.option_strings)

            #  Default optstring is `-l, --long-opt`
            optstring = ", ".join(opts)

            #  If only a long option is supported, then prefix with
            #   whitepsace by the width of a short option so that all
            #   long opts start in the same column:
            if len(opts) == 1 and len(opts[0]) > 2:
                optstring = "    " + opts[0]

            #  We're done if no argument supported
            if action.nargs == 0:
                return optstring

            #  Append option argument string after `=`
            default = action.dest.upper()
            args_string = self._format_args(action, default)
            return optstring + "=" + args_string

    return lambda prog: FluxHelpFormatter(prog, max_help_position=argwidth)


def set_treedict(in_dict, key, val):
    """
    set_treedict(d, "a.b.c", 42) is like d[a][b][c] = 42
    but levels are created on demand.
    """
    path = key.split(".", 1)
    if len(path) == 2:
        set_treedict(in_dict.setdefault(path[0], {}), path[1], val)
    else:
        in_dict[key] = val


class TreedictAction(argparse.Action):
    """
    argparse action that returns a dictionary from a set of key=value option
    arguments. The ``flux.util.set_treedict()`` function is used to set
    ``key`` in the dictionary, which allows ``key`` to have embedded dots.
    e.g. ``a.b.c=42`` is like::

       dest[a][b][c] = 42

    but levels are created on demand.
    """

    def __call__(self, parser, namespace, values, option_string=None):
        result = {}
        for arg in values:
            tmp = arg.split("=", 1)
            if len(tmp) != 2:
                raise ValueError(f"Missing required value for key {arg}")
            try:
                val = json.loads(tmp[1])
            except (json.JSONDecodeError, TypeError):
                val = tmp[1]
            set_treedict(result, tmp[0], val)
            setattr(namespace, self.dest, result)


class CLIMain(object):
    def __init__(self, logger=None):
        if logger is None:
            self.logger = logging.getLogger()
        else:
            self.logger = logger

    def __call__(self, main_func):
        loglevel = int(os.environ.get("FLUX_PYCLI_LOGLEVEL", logging.INFO))
        logging.basicConfig(
            level=loglevel, format="%(name)s: %(levelname)s: %(message)s"
        )
        exit_code = 0
        try:
            main_func()
        except SystemExit as ex:  # don't intercept sys.exit calls
            exit_code = ex
        except Exception as ex:  # pylint: disable=broad-except
            exit_code = 1
            self.logger.error(str(ex))
            self.logger.debug(traceback.format_exc())
        finally:
            logging.shutdown()
            sys.exit(exit_code)


def parse_fsd(fsd_string):
    match = re.match(r".*([smhd])$", fsd_string)
    try:
        value = float(fsd_string[:-1] if match else fsd_string)
    except:
        raise ValueError("invalid Flux standard duration")
    unit = match.group(1) if match else "s"

    if unit == "m":
        seconds = timedelta(minutes=value).total_seconds()
    elif unit == "h":
        seconds = timedelta(hours=value).total_seconds()
    elif unit == "d":
        seconds = timedelta(days=value).total_seconds()
    else:
        seconds = value
    if seconds < 0 or math.isnan(seconds) or math.isinf(seconds):
        raise ValueError("invalid Flux standard duration")
    return seconds


def parse_datetime(string, now=None):
    """Parse a possibly human readable datetime string or offset

    If string starts with `+` or `-`, then the remainder of the string
    is assumed to be a duration to add or subtract to `now` in Flux Standard
    Duration.

    Otherwise, the parsedatetime package will be used to parse the input
    string.

    Args:
        string: The string to parse as datetime or offset
        now: Optional: datetime object to use as starttime of any offset

    Returns:
        A datetime object

    Raises:
        ValueError: Input string could not be converted to datetime

    """

    if now is None:
        now = datetime.now().astimezone()

    if string.startswith("+"):
        timestamp = now.timestamp() + parse_fsd(string[1:])
        return datetime.fromtimestamp(timestamp).astimezone()

    if string.startswith("-"):
        timestamp = now.timestamp() - parse_fsd(string[1:])
        return datetime.fromtimestamp(timestamp).astimezone()

    cal = Calendar()
    cal.ptc.StartHour = 0
    time_struct, status = cal.parse(string, sourceTime=now.timetuple())
    if status == 0:
        raise ValueError(f'Invalid datetime: "{string}"')
    return datetime(*time_struct[:6]).astimezone()


class OutputFormat:
    """
    Store a parsed version of the program's output format,
    allowing the fields to iterated without modifiers, building
    a new format suitable for headers display, etc...
    """

    def __init__(self, valid_headings, fmt, prepend=None):
        """
        Parse the input format fmt with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Throws an exception if any format fields do not match the allowed
        list of headings.
        """
        self.headings = valid_headings
        self.fmt = fmt
        #  Parse format into list of (string, field, spec, conv) tuples,
        #   replacing any None values with empty string "" (this makes
        #   substitution back into a format string in self.header() and
        #   self.get_format() much simpler below)
        format_list = Formatter().parse(fmt)
        self.format_list = [[s or "" for s in t] for t in format_list]

        #  Store list of requested fields in self.fields
        self._fields = [field for (_, field, _, _) in self.format_list]

        #  Throw an exception if any requested fields are invalid:
        for field in self._fields:
            if field and not field in self.headings:
                raise ValueError("Unknown format field: " + field)

        #  Prepend arbitrary string to format fields if requested
        if prepend:
            self.fmt = self.get_format_prepended(prepend)

    @property
    def fields(self):
        return self._fields

    # This should be a method, not a function since it is overridden by
    # inheriting classes
    # pylint: disable=no-self-use
    def _fmt_tuple(self, text, field, spec, conv):
        #  If field is empty string or None, then the result of the
        #   format (besides 'text') doesn't make sense, just return 'text'
        if not field:
            return text
        #  The prefix of spec and conv are stripped by formatter.parse()
        #   replace them here if the values are not empty:
        spec = ":" + spec if spec else ""
        conv = "!" + conv if conv else ""
        return "{0}{{{1}{2}{3}}}".format(text, field, conv, spec)

    def header_format(self):
        """
        Return the header row formatted by the user-provided format spec,
        which will be made "safe" for use with string headings.
        """
        format_list = []
        for (text, field, spec, _) in self.format_list:
            #  Remove number formatting on any spec:
            spec = re.sub(r"(0?\.)?(\d+)?[bcdoxXeEfFgGn%]$", r"\2", spec)
            #  Only keep fill, align, and min width of the result.
            #  This strips possible type-specific formatting spec that
            #   will not apply to a heading, but keeps width and alignment.
            match = re.match(r"(.?[<>=^])?(\d+)", spec)
            if match is None:
                spec = ""

            #  Remove any conversion, these do not make sense for headings
            format_list.append(self._fmt_tuple(text, field, spec, None))
        fmt = "".join(format_list)
        return fmt

    def header(self):
        return self.header_format().format(**self.headings)

    def get_format_prepended(self, prepend):
        """
        Return the format string, ensuring that the string in "prepend"
        is prepended to each format field
        """
        lst = []
        for (text, field, spec, conv) in self.format_list:
            # If field doesn't have 'prepend' then add it
            if field and not field.startswith(prepend):
                field = prepend + field
            lst.append(self._fmt_tuple(text, field, spec, conv))
        return "".join(lst)

    def get_format(self):
        """
        Return the format string
        """
        return self.fmt

    def format(self, obj):
        """
        format object with internal format
        """
        try:
            retval = self.get_format().format(obj)
        except KeyError as exc:
            typestr = type(obj)
            raise KeyError(f"Invalid format field {exc} for {typestr}")
        return retval


class Tree:
    """Very simple pstree-like display for the console

    Args:
        label: Label for this node
        prefix: Text which comes before connector and label
        combine_children: combine like children as they are added
    """

    Connectors = namedtuple("Connectors", ["PIPE", "TEE", "ELBOW", "BLANK"])

    connector_styles = {
        "ascii": Connectors("|   ", "|-- ", "`-- ", "    "),
        "box": Connectors("│   ", "├── ", "└── ", "    "),
        "compact": Connectors("│ ", "├─", "└─", "  "),
    }

    def __init__(self, label, prefix="", combine_children=False):

        self.label = label
        self.prefix = prefix
        if self.prefix:
            self.prefix += " "

        #  dictionary of prefix/label for duplicate child tracking:
        self.duplicates = {}
        self.duplicate_count = 1

        self.children = []

        #  True if attempt to combine duplicate children should be
        #   made as they are added to the tree with self.append()
        #   and self.append_tree():
        self.combine_children = combine_children

    def append(self, label, prefix=""):
        """Append a new child, possibly combining duplicates

        Args:
            label: Label for this node
            prefix: Optional prefix to display before tree part

        Returns:
            A new or existing Tree object (if duplicate)

        """
        if not self.combine_children:
            return self.add(label, prefix, combine_children=False)

        #  Create a key and check for existing duplicate children.
        #  If found, simply increment the count of the existing node,
        #  O/w, add a new child and new duplicate label
        #
        clabel = f"{prefix}{label}"
        if clabel in self.duplicates:
            self.duplicates[clabel].increment()
        else:
            tree = self.add(label, prefix, combine_children=True)
            self.duplicates[clabel] = tree
        return self.duplicates[clabel]

    def append_tree(self, tree):
        """Add a child Tree, combining duplicates

        Args:
            tree: the Tree object to append

        Returns:
            A new or existing Tree object (if duplicate)
        """
        if not self.combine_children or tree.children:
            self.children.append(tree)
            return tree
        clabel = f"{tree.prefix}{tree.label}"
        if clabel in self.duplicates:
            self.duplicates[clabel].increment()
        else:
            self.children.append(tree)
            self.duplicates[clabel] = tree
        return self.duplicates[clabel]

    def add(self, label, prefix="", combine_children=False):
        """Add a child by prefix and label, does not combine duplicates

        Args:
            label: Label for this node
            prefix: Optional prefix to display before tree part
            combine_children: True if like children should be combined

        Returns:
            Tree: the new child Tree object
        """
        tree = Tree(label, prefix=prefix, combine_children=combine_children)
        self.children.append(tree)
        return tree

    def increment(self):
        self.duplicate_count = self.duplicate_count + 1

    def _render(
        self,
        pstack=None,
        connector="",
        style="box",
        max_level=None,
        truncate=None,
    ):

        #  `pstack` is a stack of prefix characters used during
        #   recursive walk of the Tree. This stack maintains the
        #   extra prefix characters required before printing the
        #   current tree connector and node label.
        #
        #  Note: the connector at the top of the stack is always
        #   the connector required for the _next_ level down in the
        #   tree. This is because we know what the prefix connector
        #   should be at this level, not at the subsequent level.
        #
        if pstack is None:
            pstack = []

        if max_level is not None and len(pstack) > max_level:
            return

        if self.duplicate_count > 1:
            label = f"{self.duplicate_count}*[{self.label}]"
        else:
            label = self.label

        #  As noted above, we need to prefix this node with all characters
        #   in `pstack` _except_ the character at the top of the stack,
        #    which applies to the next level down.
        #
        prefix = "".join(pstack[:-1])

        #  Generate the current line, truncating at 'truncate'
        #   characters if necessary
        #
        result = f"{self.prefix}{prefix}{connector}{label}"
        if truncate and len(result) > truncate:
            result = result[: truncate - 1] + "+"

        print(result)

        cstyle = self.connector_styles[style]

        for child in self.children:
            if child == self.children[-1]:
                #  If this is the last child, then push a BLANK prefix for
                #   two levels down and an ELBLOW to connect this child
                #   to its parent.
                #
                pstack.append(cstyle.BLANK)
                connector = cstyle.ELBOW
            else:
                #  Otherwise, push a PIPE prefix for two levels down
                #   and a TEE to connect this child to its parent.
                #
                pstack.append(cstyle.PIPE)
                connector = cstyle.TEE

            #  Finally, render this child and its children:
            #
            # pylint: disable=protected-access
            child._render(
                pstack,
                connector=connector,
                style=style,
                max_level=max_level,
                truncate=truncate,
            )
            pstack.pop()

    def render(
        self,
        style="box",
        level=None,
        skip_root=False,
        truncate=True,
    ):
        """Render a Tree to the console

        Args:
            style (str): style of connectors, "ascii" for ascii or "box" for
                         unicode box drawing characters (default="box")
            level (int): stop traversing at tree depth of ``level``.
            skip_root(bool): Do not include root of tree in rendered output
            truncate(bool): Chop long lines at current terminal width
                            (or 132 columns if COLUMNS variable not set
                            and current terminal width cannot be determined)
        """
        limit = None
        if truncate:
            limit = shutil.get_terminal_size((132, 25)).columns

        if skip_root:
            for child in self.children:
                child.render(
                    style=style,
                    level=level,
                    truncate=truncate,
                )
            return
        self._render(
            style=style,
            max_level=level,
            truncate=limit,
        )
