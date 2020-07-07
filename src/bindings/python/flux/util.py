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
from datetime import timedelta
from string import Formatter

import six

from flux.core.inner import ffi, raw

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
            exception_tuple = sys.exc_info()
            try:
                future = (
                    calling_obj.handle
                    if hasattr(calling_obj, "handle")
                    else calling_obj
                )
                errmsg = raw.flux_future_error_string(future)
            except EnvironmentError:
                six.reraise(*exception_tuple)
            if errmsg is None:
                six.reraise(*exception_tuple)
            raise EnvironmentError(error.errno, errmsg.decode("utf-8"))

    return func_wrapper


def encode_payload(payload):
    if payload is None or payload == ffi.NULL:
        payload = ffi.NULL
    elif isinstance(payload, six.text_type):
        payload = payload.encode("UTF-8")
    elif not isinstance(payload, six.binary_type):
        payload = json.dumps(payload, ensure_ascii=False).encode("UTF-8")
    return payload


def encode_topic(topic):
    # Convert topic to utf-8 binary string
    if topic is None or topic == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "Topic must not be None/NULL")
    if isinstance(topic, six.text_type):
        topic = topic.encode("UTF-8")
    elif not isinstance(topic, six.binary_type):
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
        for (text, field, spec, conv) in self.format_list:
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
        return self.get_format().format(obj)
