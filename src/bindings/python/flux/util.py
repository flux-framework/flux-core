###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import base64
import copy
import errno
import glob
import json
import logging
import math
import os
import re
import shutil
import signal
import stat
import sys
import threading
import traceback
from collections import abc, namedtuple
from datetime import datetime, timedelta
from operator import attrgetter
from pathlib import Path, PurePosixPath
from string import Formatter
from typing import Mapping

import yaml
from wcwidth import wcswidth

# tomllib added to standard library in Python 3.11
# flux-core minimum is Python 3.6.
try:
    import tomllib  # novermin
except ModuleNotFoundError:
    from flux.utils import tomli as tomllib

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


def help_formatter(argwidth=40, raw_description=False):
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
            #   whitespace by the width of a short option so that all
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

    class FluxRawDescriptionHelpFormatter(
        FluxHelpFormatter, argparse.RawDescriptionHelpFormatter
    ):
        pass

    if raw_description:
        return lambda prog: FluxRawDescriptionHelpFormatter(
            prog, max_help_position=argwidth
        )

    return lambda prog: FluxHelpFormatter(prog, max_help_position=argwidth)


def get_treedict(in_dict, key, default=None):
    """
    get_treedict(d, "a.b.c" [,default=None]) is like:
    >>> try:
    >>>     return d[a][b][c]
    >>> except KeyError:
    >>>     return default
    """
    if "." in key:
        next_key, rest = key.split(".", 1)
        try:
            return get_treedict(in_dict[next_key], rest, default)
        except KeyError:
            return default
    return in_dict.get(key, default)


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


def del_treedict(in_dict, key, remove_empty=False):
    """
    ``del_treedict(d, "a.b.c")`` is like ``del d[a][b][c]``

    Args:
        in_dict (Mapping): dict in which to remove ``key``
        key (str): dotted key to remove
        remove_empty (bool): If True, remove empty dictionaries that result
            from deletion of ``key``.

    Raises:
        KeyError: a path component of ``key`` does not exist.
    """
    path = key.split(".", 1)
    if len(path) == 2:
        del_treedict(in_dict[path[0]], path[1], remove_empty=remove_empty)
        if (
            remove_empty
            and isinstance(in_dict[path[0]], abc.Mapping)
            and len(in_dict[path[0]]) == 0
        ):
            del in_dict[path[0]]
    else:
        del in_dict[key]


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


class FilterAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values)
        setattr(namespace, "filtered", True)


class FilterActionSetUpdate(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, "filtered", True)
        values = values.split(",")
        getattr(namespace, self.dest).update(values)


class FilterActionUser(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, "filtered", True)
        setattr(namespace, "filtereduser", True)
        setattr(namespace, self.dest, values)


# pylint: disable=redefined-builtin
class FilterTrueAction(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest,
        const=True,
        default=False,
        required=False,
        help=None,
        metavar=None,
    ):
        super(FilterTrueAction, self).__init__(
            option_strings=option_strings,
            dest=dest,
            nargs=0,
            const=const,
            default=default,
            help=help,
        )

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, self.const)
        setattr(namespace, "filtered", True)


class YesNoAction(argparse.Action):
    """Simple argparse.Action for options with yes|no arguments"""

    def __init__(
        self,
        option_strings,
        dest,
        help=None,
        metavar="[yes|no]",
    ):
        super().__init__(
            option_strings=option_strings, dest=dest, help=help, metavar=metavar
        )

    def __call__(self, parser, namespace, value, option_string=None):
        if value not in ["yes", "no"]:
            raise ValueError(f"{option_string} requires either 'yes' or 'no'")
        setattr(namespace, self.dest, value == "yes")


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
            # Prefer '{strerror}: {filename}' error message over default
            # OSError string representation which includes useless
            # `[Error N]` prefix in output.
            errmsg = getattr(ex, "strerror", None) or str(ex)
            if getattr(ex, "filename", None):
                errmsg += f": '{ex.filename}'"
            self.logger.error(errmsg)
            self.logger.debug(traceback.format_exc())
        finally:
            logging.shutdown()
            sys.exit(exit_code)


def parse_fsd(fsd_string):
    # Special case for RFC 23 "infinity"
    if fsd_string in ["inf", "infinity", "INF", "INFINITY"]:
        return float("inf")

    match = re.match(r"(.*?)(s|m|h|d|ms)$", fsd_string)
    try:
        value = float(match.group(1) if match else fsd_string)
    except:
        raise ValueError("invalid Flux standard duration")
    unit = match.group(2) if match else "s"

    if unit == "m":
        seconds = timedelta(minutes=value).total_seconds()
    elif unit == "h":
        seconds = timedelta(hours=value).total_seconds()
    elif unit == "d":
        seconds = timedelta(days=value).total_seconds()
    elif unit == "ms":
        seconds = timedelta(milliseconds=value).total_seconds()
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

    if string == "now":
        return now

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


class UtilDatetime(datetime):
    """
    Subclass of datetime but with a __format__ method that supports
    width and alignment specification after any time formatting via
    two colons `::` before the Python format spec, e.g::

    >>> "{0:%b%d %R::>16}".format(UtilDatetime.now())
    '     Sep21 18:36'
    """

    def __format__(self, fmt):
        # The string "::" is used to split the strftime() format from
        # any Python format spec:
        timefmt, *spec = fmt.split("::", 1)

        if self == datetime.fromtimestamp(0.0):
            result = ""
        else:
            # Call strftime() to get the formatted datetime as a string
            result = self.strftime(timefmt or "%FT%T")

        spec = spec[0] if spec else ""

        # Handling of the 'h' suffix on spec is required here, since the
        # UtilDatetime format() is called _after_ UtilFormatter.format_field()
        # (where this option is handled for other types)
        if spec.endswith("h"):
            if not result:
                result = "-"
            spec = spec[:-1] + "s"

        # If there was a format spec, apply it here:
        if spec:
            return f"{{0:{spec}}}".format(result)
        return result


def fsd(secs):
    # Special case for RFC 23 "infinity"
    # N.B. We return lower case "inf" to match Python's "math.inf".
    if math.isinf(secs):
        return "inf"

    #  Round <1ms down to 0s for now
    if secs < 1.0e-3:
        strtmp = "0s"
    elif secs < 10.0:
        strtmp = "%.03fs" % secs
    elif secs < 60.0:
        strtmp = "%.4gs" % secs
    elif secs < (60.0 * 60.0):
        strtmp = "%.4gm" % (secs / 60.0)
    elif secs < (60.0 * 60.0 * 24.0):
        strtmp = "%.4gh" % (secs / (60.0 * 60.0))
    else:
        strtmp = "%.4gd" % (secs / (60.0 * 60.0 * 24.0))
    return strtmp


def empty_outputs():
    localepoch = datetime.fromtimestamp(0.0).strftime("%FT%T")
    empty = ("", "0s", "0.0", "0:00:00", "1970-01-01T00:00:00", localepoch)
    return empty


class UtilFormatter(Formatter):
    # pylint: disable=too-many-branches

    def convert_field(self, value, conv):
        """
        Flux utility-specific field conversions. Avoids the need
        to create many different format field names to represent
        different conversion types.
        """
        orig_value = str(value)
        if conv == "d":
            # convert from float seconds since epoch to a datetime.
            # User can than use datetime specific format fields, e.g.
            # {t_inactive!d:%H:%M:%S}.
            try:
                value = UtilDatetime.fromtimestamp(value)
            except TypeError:
                if orig_value == "":
                    value = UtilDatetime.fromtimestamp(0.0)
                else:
                    raise
        elif conv == "D":
            # the JobInfo class initializes many timestamps to 0.0 if
            # they are not available.  Treat epoch time as special case
            # and just return empty string.
            if not value:
                return ""
            # As above, but convert to ISO 8601 date time string.
            try:
                value = datetime.fromtimestamp(value).strftime("%FT%T")
            except TypeError:
                if orig_value == "":
                    value = ""
                else:
                    raise
        elif conv == "F":
            # convert to Flux Standard Duration (fsd) string.
            try:
                value = fsd(value)
            except TypeError:
                if orig_value == "":
                    value = ""
                else:
                    raise
        elif conv == "H":
            # if > 0, always round up to at least one second to
            #  avoid presenting a nonzero timedelta as zero
            try:
                if 0 < value < 1:
                    value = 1
                value = str(timedelta(seconds=round(value)))
            except TypeError:
                if orig_value == "":
                    value = ""
                else:
                    raise
        elif conv == "P":
            #  Convert a floating point to percentage
            try:
                value = value * 100
                if 0 < value < 1:
                    value = f"{value:.2f}%"
                else:
                    value = f"{value:.3g}%"
            except (TypeError, ValueError):
                if orig_value == "":
                    value = ""
                else:
                    raise
        else:
            value = super().convert_field(value, conv)
        return value

    def format_field(self, value, spec):

        denote_truncation = False
        if spec.endswith("+"):
            denote_truncation = True
            spec = spec[:-1]

        # Note: handling of the 'h' suffix for UtilDatetime objects
        # must be deferred to the UtilDatetetime format() method, since
        # that method will be called after this one:
        if spec.endswith("h") and not isinstance(value, UtilDatetime):
            basecases = empty_outputs()
            value = "-" if str(value) in basecases else str(value)
            spec = spec[:-1] + "s"

        # Normal python output will not consider emoji width with
        # width formatting.  So we will adjust the output width of
        # field accordingly to account for it if the "W" modifier
        # is specified.
        if spec.endswith("W") and isinstance(value, str):
            match = re.search(r"^([<>])(\d+)W", spec)
            if match:
                align = match[1]
                width = int(match[2])
                display_width = wcswidth(value)
                normal_width = len(value)
                width_diff = display_width - normal_width
                if width_diff > 0 and width > width_diff:
                    width -= width_diff
                    spec = f"{align}{width}s"
        # if spec was not modified above, need to convert "W" to "s"
        if spec.endswith("W"):
            spec = spec[:-1] + "s"

        retval = super().format_field(value, spec)

        if denote_truncation and len(retval) < len(str(value)):
            retval = retval[:-1] + "+"

        return retval


class AltField:
    """
    Convenient wrapper for fields that have an ascii and non-ascii
    representation. Allows the ascii representation to be selected with
    {field.ascii}.
    """

    def __init__(self, default, ascii):
        self.default = default
        self.ascii = ascii

    def __str__(self):
        return self.default

    def __format__(self, fmt):
        return str(self).__format__(fmt)


class OutputFormat:
    """Extended output format container class for Flux utilities.

    The OutputFormat class stores a parsed version of a program's output
    format, allowing fields to be iterated without their corresponding
    modifiers, automatic creation of a format string suitable for header
    display, as well as some other features specific to Flux utility
    output including:

     - Support for format prefix "sentinels" ``?:``, ``+:`` and ``?+:``
       which suppress fields that would be empty, expand fields automatically
       to their maximum obsevered width, or both.
     - Support for embedding a set of sort keys in a format via a
       ``sort:KEY,...`` prefix.
     - A ``print_items()`` method to handle the common code for sorting,
       filtering and printing a set of items using the current format.

    Attributes:
        formatter (Formatter): a custom string formatter that will be used to
            render output using its ``format()`` method. The default
            formatter is :py:class:`flux.util.UtilFormatter`
        headings (dict): A mapping of field name to header string. This also
            provides detection of invalid fields, so all valid fields should
            have an entry in headings. The headings dict can be set as an
            argument to :py:func:`OutputFormat.__init__`, or a subclass of
            the :py:class`flux.util.OutputFormat` class may set default
            headings for a given tool.
    """

    formatter = UtilFormatter
    headings: Mapping[str, str] = {}

    class HeaderFormatter(UtilFormatter):
        """Custom formatter for flux utilities header row.

        Override default formatter behavior of calling getattr() on dotted
        field names. Instead look up header literally in kwargs.
        This greatly simplifies header name registration as well as
        registration of "valid" fields.
        """

        def get_field(self, field_name, args, kwargs):
            """Override get_field() so we don't do the normal gettatr thing"""
            if field_name in kwargs:
                return kwargs[field_name], None
            return super().get_field(field_name, args, kwargs)

    def __init__(
        self, fmt, headings=None, prepend="0.", nullify_expansion_sentinel=False
    ):
        """
        Parse the input format ``fmt`` with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Args:
            headings (dict): Set a mapping of field name to header string.
            prepend (str): Prepend each field with a string. (Default is
                "0.", so ``{f1} {f2}`` becomes ``{0.f1} {0.f2}``.
            nullify_expansion_sentinel (bool): Replace fields with an expansion
                sentinel (``+:``) with just the field, e.g. ``+:{foo:<10.10}``
                is replaced with ``{foo}``.
        """
        if headings is not None:
            self.headings = headings
        if prepend is not None:
            self.prepend = prepend
        #  Parse format into list of (string, field, spec, conv) tuples,
        #   replacing any None values with empty string "" (this makes
        #   substitution back into a format string in self.header() and
        #   self.get_format() much simpler below)
        format_list = Formatter().parse(fmt)
        self.format_list = [[s or "" for s in t] for t in format_list]

        #  If the first entry's text portion starts with `sort:`, then
        #  what follows is a list of sort keys (used only in print_items())
        #
        #  self.sort_keys is a list of (key(str), reverse(bool)) tuples.
        self.sort_keys = []
        if self.format_list and self.format_list[0][0].startswith("sort:"):
            #  Remove sort:keys, prefix from format_list, replace with
            #   empty string:
            sort_spec = self.format_list[0][0]
            self.format_list[0][0] = ""

            #  Store sort keys for later use
            self.set_sort_keys(sort_spec[5:])

            #  Strip sort_spec from beginning of fmt
            fmt = fmt.lstrip(sort_spec)

        #  Save format after removal of sort keys:
        self.fmt = fmt
        self.fmt_orig = fmt

        #  Store list of requested fields in self.fields
        #  (ignore any empty fields, which may be present due to text
        #   at the end of a format string):
        self._fields = [field for (_, field, _, _) in self.format_list if field]

        #  Throw an exception if any requested fields are invalid:
        for field in self._fields:
            #  Remove any "0." prefix:
            field = field[2:] if field.startswith("0.") else field
            if field and field not in self.headings:
                raise ValueError("Unknown format field: " + field)

        #  Prepend arbitrary string to format fields if requested
        if self.prepend:
            self.fmt = self.get_format_prepended(
                self.prepend, include_sort_prefix=False
            )

    class FormatSpec:
        """Split Python format spec into components"""

        # Note: 'hyphen' and 'truncate' are Flux extensions
        components = (
            "fill",
            "align",
            "sign",
            "pos_zero",
            "alt",
            "zero_pad",
            "width_str",
            "grouping",
            "decimal",
            "precision_str",
            "type",
            "hyphen",
            "wide",
            "truncate",
        )

        def __init__(self, spec):

            # If spec contains two colons ('::') then this is a special
            # datetime conversion spec and the format spec is after the
            # two colons:
            if "::" in spec:
                spec = spec[spec.find("::") + 2 :]

            # Regex taken from https://stackoverflow.com/a/78351366
            # 'hyphen' and 'truncate' are Flux extensions
            spec_re = re.compile(
                r"(?:(?P<fill>[\s\S])?(?P<align>[<>=^]))?"
                r"(?P<sign>[- +])?"
                r"(?P<pos_zero>z)?"
                r"(?P<alt>#)?"
                r"(?P<zero_pad>0)?"
                r"(?P<width_str>\d+)?"
                r"(?P<grouping>[_,])?"
                r"(?:(?P<decimal>\.)(?P<precision_str>\d+))?"
                r"(?P<type>[bcdeEfFgGnosxX%])?"
                r"(?P<hyphen>h)?"
                r"(?P<wide>W)?"
                r"(?P<truncate>\+)?"
            )
            try:
                self._spec_dict = spec_re.fullmatch(spec).groupdict()
            except AttributeError:
                self._spec_dict = {}

            for item in self.components:
                value = self._spec_dict.get(item)
                setattr(self, item, "" if value is None else value)

        @property
        def width(self):
            return int(self.width_str) if self.width_str else ""

        @width.setter
        def width(self, val):
            self.width_str = str(val)

            # Also adjust precision if necessary (only for string type)
            if (
                self.type in (None, "s", "")
                and self.precision
                and self.precision < self.width
            ):
                self.precision = self.width

        @property
        def precision(self):
            return int(self.precision_str) if self.precision_str else 0

        @precision.setter
        def precision(self, val):
            self.precision_str = str(val)

        def __str__(self):
            result = ""
            for item in self.components:
                value = getattr(self, item)
                if value is not None:
                    result += str(value)
            return result

    @property
    def fields(self):
        # Add any sort keys to returned fields for users like flux-jobs(1),
        # which need to translate requested fields to job-list attributes:
        fields = set(self._fields.copy())
        if self.sort_keys:
            fields.update([x for x, y in self.sort_keys])
        return list(fields)

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
        Return the current format sanitized for use in formatting a header
        row. All format specs are adjusted to keep only the align and width,
        and any numeric formatting types are dropped since the header row
        is made up of only strings.
        """
        format_list = []
        for text, field, spec, _ in self.format_list:
            spec = self.FormatSpec(spec)

            #  Only keep align and min width of the result.
            #  This strips possible type-specific formatting spec that
            #   will not apply to a heading, but keeps width and alignment.
            spec = f"{spec.align}{spec.width}"

            #  Remove any conversion, these do not make sense for headings
            format_list.append(self._fmt_tuple(text, field, spec, None))
        fmt = "".join(format_list)
        return fmt

    def header(self):
        """Return a header row using the current format"""
        formatter = self.HeaderFormatter()
        return formatter.format(self.header_format(), **self.headings)

    def get_format_prepended(
        self,
        prepend,
        except_fields=None,
        include_sort_prefix=True,
        nullify_expansion_sentinel=False,
    ):
        """
        Return the format string, ensuring that the string in "prepend"
        is prepended to each format field

        Args:
            prepend (str): String to prepend to each field name, e.g. "0."
            except_fields (list): List of fields to skip  when constructing
                the format.
            include_sort_prefix (bool): If format specifies a list of sort
                keys, include them in a ``sort:`` prefix. Default: True
            nullify_expansion_sentinel (bool): If format specifies an expansion
                prefix ``+:``, then substitute the field without any width
                and precision, e.g. ``+:{foo:<10.10}`` becomes ``{foo}``.
        """
        if except_fields is None:
            except_fields = []
        lst = []
        for text, field, spec, conv in self.format_list:
            # Skip this field if it is in except_fields
            if field in except_fields:
                # Preserve any format "prefix" (i.e. the text):
                # Unless it is whitespace or a sentinel (+:,  etc):
                result = text.strip()
                if result and result not in ("+:", "?:", "?+:"):
                    lst.append(text)
                continue
            # If field doesn't have 'prepend' then add it
            if field and not field.startswith(prepend):
                field = prepend + field
            # If caller has requested `+:` prefix is to be nullified,
            # and that prefix exists, then reset text and spec:
            if nullify_expansion_sentinel and text.endswith("+:"):
                text = ""
                spec = ""
            lst.append(self._fmt_tuple(text, field, spec, conv))
        if include_sort_prefix:
            prefix = self._sort_prefix()
        else:
            prefix = ""
        return prefix + "".join(lst)

    def _sort_prefix(self):
        if self.sort_keys:
            keys = [f"-{x}" if rev else x for x, rev in self.sort_keys]
            return "sort:" + ",".join(keys) + " "
        return ""

    def get_format(self, orig=False, include_sort_prefix=True):
        """
        Return the format string
        Args:
            orig (bool): Return the format as originally specified.
            include_sort_prefix (bool): If the format includes a list of
                sort keys, include a ``sort:`` prefix in the result.
        """
        fmt = self.fmt_orig if orig else self.fmt
        if include_sort_prefix:
            return self._sort_prefix() + fmt
        return fmt

    def copy(self, except_fields=None, nullify_expansion=False):
        """
        Return a copy of the current formatter, optionally with some
        fields removed

        Args:
            except_fields (list): List of fields to remove from result.
            nullify_expansion (bool): If True, replace any fields in original
                that have an expansion sentinel with just the field, e.g.
                ``+:{foo:>5.5}`` becomes ``{foo}`` in the copy.
        """
        cls = self.__class__
        return cls(
            self.get_format_prepended(
                "", except_fields, nullify_expansion_sentinel=nullify_expansion
            ),
            headings=self.headings,
            prepend=self.prepend,
        )

    def format(self, obj):
        """Format object with internal format"""
        try:
            retval = self.formatter().format(
                self.get_format(include_sort_prefix=False), obj
            )
        except KeyError as exc:
            typestr = type(obj)
            raise KeyError(f"Invalid format field {exc} for {typestr}")
        return retval

    def filter(self, items, no_header=False):
        """
        Check for format fields that are prefixed with `?:` (e.g. "?:{name}"),
        and filter them out of the current format string if they result in an
        empty value (as defined by the `empty` tuple defined below) for every
        entry in `items`.

        Also, check for fields prefixed with `+:` (e.g. "+:{name}") and expand
        those fields to the maximum observed width.

        (`?+:` requests both actions: filter out field if it is empty for all
        items, if not expand to maximum width)

        Args:
            items (iterable): list of items to consider for output
            no_header (boolean): do not use header in calculations (default: False)
        """

        #  Build a list of all format strings that have one of the width
        #  adjustment sentinels to determine which are subject to the test for
        #  emptiness/maxwidth.
        #
        sentinels = {"?:": "filter", "+:": "maxwidth", "?+:": "both"}

        def sentinel_keys():
            #  helper function to iterate supported sentinels by longest
            #  first to avoid matching `+:` instead of `?+:`, etc.
            #
            return sorted(sentinels.keys(), key=lambda x: -len(x))

        lst = []
        index = 0

        #  Loop over each format entry to find entries using any one
        #  of the sentinels above:
        #
        #  Note: we remove the leading `text` and the format `spec` because
        #  these may make even empty formatted fields non-empty. E.g. a spec
        #  of `:>8` will always make the format result 8 characters wide.
        #
        for text, field, spec, conv in self.format_list:
            # Determine if field has any supported sentinel above:
            end = next((x for x in sentinel_keys() if text.endswith(x)), False)

            if end:
                #  This entry matches one of the filtering sentinels, parse
                #  the spec to get current width (and allow possible
                #  reconstruction later).
                spec = self.FormatSpec(spec)

                #  Save a format without any spec to allow determination of
                #  maximum width after formatting all items:
                fmt = self._fmt_tuple("", "0." + field, None, conv)

                #  Save the modified format, index, type, maximum width,
                #  observed width, and broken-down spec in lst:
                initialmaxwidth = spec.width or 0
                if sentinels[end] in ("maxwidth", "both"):
                    initialmaxwidth = max(
                        initialmaxwidth,
                        0 if no_header else len(self.headings.get(field, "")),
                    )
                lst.append(
                    dict(
                        fmt=fmt,
                        index=index,
                        type=sentinels[end],
                        maxwidth=initialmaxwidth,
                        width=0,
                        spec=spec,
                    )
                )
            index = index + 1

        # Return immediately if no format fields need filtering:
        if not lst:
            return self.get_format(orig=True)

        formatter = self.formatter()

        #  Get a list of outputs that would qualify as "empty"
        empty = empty_outputs()

        #  Loop over all items that will be printed and capture the max
        #  width. This will later be used to either filter out the format
        #  field entirely, or update the width to the maximum value:
        for item in items:
            for entry in lst:
                result = formatter.format(entry["fmt"], item)
                width = 0 if result in empty else wcswidth(result)
                if width > entry["maxwidth"]:
                    entry["maxwidth"] = width
                if width > entry["width"]:
                    entry["width"] = width

        #  Create two new lists from lst:
        #
        #  remove: These entries have 0 width and were requested to be removed
        #
        #  new_lst: lst without those entries in `remove`. These entries
        #      have nonzero width and/or are requesting max width be
        #      substituted into the format spec.
        #
        remove = []
        new_lst = []
        for entry in lst:
            if entry["width"] == 0 and entry["type"] in ("filter", "both"):
                remove.append(entry["index"])
            else:
                new_lst.append(entry)
        lst = new_lst

        #  For any remaining entries in lst, update the format spec width
        #  to the found maxwidth:
        #
        format_list = self.format_list.copy()
        for entry in lst:
            if entry["type"] in ("maxwidth", "both"):
                entry["spec"].width = entry["maxwidth"]
                format_list[entry["index"]][2] = str(entry["spec"])

        #  Remove any entries that were empty from self.format_list
        #  After this line saved indices in entry["index"] will no longer
        #  be valid, so they should not be used after this point.
        #
        format_list = [x for i, x in enumerate(self.format_list) if i not in remove]

        #  Remove sentinels from remaining entries so they disappear in output.
        for entry in format_list:
            for s in sentinel_keys():
                entry[0] = entry[0].replace(s, "")

        #  Return new format string:
        return self._sort_prefix() + "".join(self._fmt_tuple(*x) for x in format_list)

    def set_sort_keys(self, sort_keys):
        """
        Override current sort keys used in print_items() which may have
        been set by a `sort:` prefix.

        Args:
            sort_keys (str): Comma separated list of sort keys. Prefix a
                key with `-` to reverse sort on that key.
        """
        #  Reset self.sort_keys:
        self.sort_keys = []
        for key in sort_keys.split(","):
            key = key.strip()
            reverse = False
            if key.startswith("-"):
                reverse = True
                key = key[1:]
            if key not in self.headings:
                raise ValueError("Invalid sort key: " + key)
            self.sort_keys.append((key, reverse))

    def sort_items(self, items):
        """
        Sort items using sort_keys attribute.

        Args:
            items (list): list of items to sort. Note that the list is
                sorted in place and then returned.
        """
        # Apply any requested sort:
        for key, reverse in reversed(self.sort_keys):
            items.sort(key=attrgetter(key), reverse=reverse)
        return items

    def print_items(self, items, no_header=False, pre=None, post=None):
        """
        Handle printing a list of items with the current format.

        First pre-process format using ``items`` to remove any empty
        fields, if requested. (The pre-processing step could be extended
        in the future.)

        Next, sort items via any sort keys as set by set_sort_keys() or
        via a ``sort:`` prefix in the supplied format.

        Then, generate a header unless no_header is True.

        Finally output a formatted line for each provided item.

        Args:
            items (iterable): list of items to format
            no_header (boolean): disable header row (default: False)
            pre (callable): Function to call before printing each item
            post (callable): Function to call after printing each item
        """
        #  Preprocess original format by processing with filter():
        newfmt = self.filter(items, no_header=no_header)
        #  Get the current class for creating a new formatter instance:
        cls = self.__class__
        #  Create new instance of the current class from filtered format:
        formatter = cls(newfmt, headings=self.headings, prepend=self.prepend)

        items = self.sort_items(items)

        if not no_header:
            print(formatter.header())
        for item in items:
            line = formatter.format(item)
            if not line or line.isspace():
                continue
            if callable(pre):
                pre(item)
            try:
                print(line)
            except UnicodeEncodeError:
                print(line.encode("utf-8", errors="surrogateescape").decode())
            if callable(post):
                post(item)


class Deduplicator:
    """
    Generic helper to deduplicate a list of formatted items for objects
    that can be aggregated.

    Args:
        formatter (OutputFormat): Formatter instance to use for deduplication
        except_fields (list): List of fields to not consider when merging
            like lines. These are typically fields that can be combined, such
            as a node count, node list, ranks, etc.
        combine (callable): A function that is used to combine matching
            items, called as combine(existing, new).
    """

    def __init__(self, formatter, except_fields=None, combine=None):
        self.formatter = formatter.copy(
            except_fields=except_fields, nullify_expansion=True
        )
        self.combine = combine
        self.hash = {}
        self.items = []
        #  Allow class to be iterable https://stackoverflow.com/a/48670014
        self.__iter = None

    def __iter__(self):
        if self.__iter is None:
            self.__iter = iter(self.items)
        return self

    def __next__(self):
        try:
            return next(self.__iter)
        except StopIteration:  # support repeated iteration
            self.__iter = None
            raise

    def append(self, item):
        """
        Append a new item to a deduplicator. Combines item with an existing
        entry if the formatted result is identical to another entry.
        """
        key = self.formatter.format(item)
        try:
            result = self.hash[key]
            if self.combine is not None:
                self.combine(result, item)
        except KeyError:
            self.hash[key] = item
            self.items.append(item)


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


#  Slightly modified from https://stackoverflow.com/a/7205107
def dict_merge(src, new):
    "merges dict new into dict src"
    for key in new:
        if key in src:
            if isinstance(src[key], dict) and isinstance(new[key], dict):
                dict_merge(src[key], new[key])
            elif src[key] == new[key]:
                pass  # same leaf value
            else:
                #  Default is to override:
                src[key] = new[key]
        else:
            src[key] = new[key]
    return src


def xdg_searchpath(subdir=""):
    """
    Build standard Flux config search path based on XDG specification
    """
    #  Start with XDG_CONFIG_HOME (or ~/.config) since it is the
    #  highest precedence:
    confdirs = [os.getenv("XDG_CONFIG_HOME") or f"{Path.home()}/.config"]

    #  Append XDG_CONFIG_DIRS as colon separated path (or /etc/xdg)
    #  Note: colon separated paths are in order of precedence.
    confdirs += (os.getenv("XDG_CONFIG_DIRS") or "/etc/xdg").split(":")

    #  Append "/flux" (with optional subdir) to all members of
    #  confdirs to build searchpath:
    return [Path(directory, "flux", subdir) for directory in confdirs]


class UtilConfig:
    """
    Very simple class for loading hierarchical configuration for Flux
    Python utilities. Configuration is loaded as a dict from an optional
    initial dict, overriding from XDG system and user base directories
    in that order.

    Config files are loaded from <name>.<ext>, where ext can be one
    of json, yaml, or toml. If multiple files exist they are processed
    in glob(3) order.

    Args:
        name: config name, used as the stem of config file to load
              this is typically the name of the tool
        toolname (optional): actual name of the tool/utility if "name" is
                             different.  used in environment variable lookup.
        subcommand (optional): name of subcommand. Used as name of subtable
                               in configuration to use.
        initial_dict: Set of default values (optional)

    """

    extension_handlers = {
        ".toml": tomllib.load,
        ".json": json.load,
        ".yaml": yaml.safe_load,
    }

    builtin_formats = {}

    def __init__(self, name, toolname=None, subcommand=None, initial_dict=None):
        self.name = name
        self.toolname = toolname
        self.dict = {}
        if initial_dict:
            self.dict = copy.deepcopy(initial_dict)
        self.config = self.dict

        #  If not None,  use subcommand config in subtable = 'subtable'
        self.subtable = subcommand

        #  Build config search path in precedence order based on XDG
        #  specification. Later this will be reversed since we want
        #  to traverse the paths in _reverse_ precedence order in this
        #  implementation.
        self.searchpath = xdg_searchpath()

        #  Reorder searchpath into reverse precedence order
        self.searchpath.reverse()

    def load(self):
        """Load configuration from current searchpath

        Returns self so that constructor and load() can be called like

        >>> config = UtilConfig("myutil").load()

        """

        for path in self.searchpath:
            for filepath in sorted(glob.glob(f"{path}/{self.name}.*")):
                conf = {}
                ppath = PurePosixPath(filepath)

                # ignore files with unsupported extensions:
                if ppath.suffix not in self.extension_handlers:
                    continue

                try:
                    with open(filepath, "rb") as ofile:
                        conf = self.extension_handlers[ppath.suffix](ofile)
                except (
                    tomllib.TOMLDecodeError,
                    json.decoder.JSONDecodeError,
                    yaml.scanner.ScannerError,
                ) as exc:
                    #  prepend file path to decode exceptions in case it
                    #  it is missing (e.g. tomllib)
                    raise ValueError(f"{filepath}: {exc}") from exc

                self.validate(filepath, conf)

                dict_merge(self.dict, conf)

        # If a subtable is set, then update self.config:
        if self.subtable and self.subtable in self.dict:
            self.config = self.dict[self.subtable]

        return self

    def validate_formats(self, path, formats):
        """
        Convenience function to validate a set of formats in config loaded
        from path in the common flux-* formats config schema, i.e a dictionary
        of format names in the form:

            { "name": {
                "description": "format description",
                "format":      "format",
              },
              "name2": ..
            }

        Raises ValueError on failure.
        """
        builtin_formats = self.builtin_formats
        #  If we're working with a subtable, then choose the same
        #  subtable from builtin_formats if it exists:
        if self.subtable and self.subtable in self.builtin_formats:
            builtin_formats = self.builtin_formats[self.subtable]

        if not isinstance(formats, dict):
            raise ValueError(f"{path}: the 'formats' key must be a mapping")
        for name, entry in formats.items():
            if name in builtin_formats:
                raise ValueError(
                    f"{path}: override of builtin format '{name}' not permitted"
                )
            if not isinstance(entry, dict):
                raise ValueError(f"{path}: 'formats.{name}' must be a mapping")
            if "format" not in entry:
                raise ValueError(
                    f"{path}: formats.{name}: required key 'format' missing"
                )
            if not isinstance(entry["format"], str):
                raise ValueError(
                    f"{path}: formats.{name}: 'format' key must be a string"
                )

    def validate(self, path, conf):
        """
        Validate config file as it is loaded before merging it with the
        configuration. This function does nothing in the base class, but
        subclasses may define it to implement higher level validation.
        """

    def _default_env_name(self):
        """
        Get tool default environment variable (name upper case, s/-/_/g)
        """
        name = self.toolname if self.toolname else self.name
        prefix = name.upper().replace("-", "_")
        if self.subtable:
            prefix += "_" + self.subtable.upper().replace("-", "_")
        return prefix + "_FORMAT_DEFAULT"

    def get_format_string(self, format_name):
        """
        Convenience function to fetch a format string from the current
        config. Assumes the current config has been loaded and contains
        a "formats" table.

        Special format names:
            help: print a list of configured formats
            get-config=NAME: dump the config for format "name"

        Args:
            format_name: Name of the configured format to return
        """
        if "{" in format_name:
            return format_name

        formats = self.formats
        if format_name == "help":
            print(f"\nConfigured {self.name} output formats:\n")
            for name, entry in formats.items():
                print(f"  {name:<12}", end="")
                try:
                    print(f" {entry['description']}")
                except KeyError:
                    print()
            sys.exit(0)
        elif format_name.startswith("get-config="):
            _, name = format_name.split("=", 1)
            try:
                entry = formats[name]
            except KeyError:
                raise ValueError(f"--format: No such format {name}")
            if self.subtable:
                print(f"[{self.subtable}.formats.{name}]")
            else:
                print(f"[formats.{name}]")
            if "description" in entry:
                print(f"description = \"{entry['description']}\"")
            print(f"format = \"{entry['format']}\"")
            sys.exit(0)
        elif format_name == "default":
            # Default can be overridden by environment variable
            try:
                format_name = os.environ[self._default_env_name()]
                if "{" in format_name:
                    return format_name
            except KeyError:
                # no environment var, fallthrough
                pass

        try:
            return formats[format_name]["format"]
        except KeyError:
            raise ValueError(f"--format: No such format {format_name}")

    def __getattr__(self, attr):
        return self.config[attr]


class Fileref(dict):
    """
    Construct a RFC 37 data file object from a data parameter and optional
    permissions and encoding. ``Fileref`` is a subclass of ``dict`` so it
    may be used in place of a dict and is directly serializable to JSON.

    If ``data`` is a dict, then a file object with no encoding is created
    and the dict is stored in ``data`` for eventual encoding as JSON
    content. Otherwise, if ``encoding`` is set, then data is presumed to
    already be encoded and is stored verbatim.  If data is not a dict,
    and encoding is not set, then data is assumed to be the path to file
    in the filesystem, in which case the encoding will be 'utf-8' if the
    file contents can be encoded as such, otherwise 'base64'.

    Args:
        data (dict, str, bytes): File data or path as explained above.
        perms: File permissions (default 0600 octal)
        encoding: Explicit encoding if `data` is not a dict or filename.
    """

    def __init__(self, data, perms=0o0600, encoding=None):
        ref = {"mode": perms | stat.S_IFREG}
        if isinstance(data, dict):
            ref["data"] = data
        elif encoding is not None:
            if encoding not in ("base64", "utf-8"):
                raise ValueError("invalid encoding: {encoding}")
            ref["data"] = data
            ref["encoding"] = encoding
        else:
            st = os.stat(data)
            ref["size"] = st.st_size
            ref["mode"] = st.st_mode
            with open(data, "rb") as infp:
                try:
                    ref["data"] = infp.read().decode("utf-8")
                    ref["encoding"] = "utf-8"
                except UnicodeError:
                    infp.seek(0)
                    ref["data"] = base64.b64encode(infp.read()).decode("utf-8")
                    ref["encoding"] = "base64"
        super().__init__(**ref)
