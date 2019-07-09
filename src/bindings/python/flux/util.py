###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import os
import re
import sys
import errno
import json
import logging
import os
import math
from datetime import timedelta

import six

from flux.core.inner import ffi, raw

__all__ = [
    "check_future_error",
    "encode_payload",
    "encode_topic",
    "CLIMain",
    "parse_fsd",
    "modfind",
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
    # Convert payload to ffi.NULL or utf-8 string
    if payload is None or payload == ffi.NULL:
        return ffi.NULL
    try:
        return six.ensure_binary(payload)
    except TypeError:
        return json.dumps(payload, ensure_ascii=False).encode("UTF-8")

def encode_topic(topic):
    # Convert topic to utf-8 string
    if topic is None or topic == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "Topic must not be None/NULL")
    try:
        return six.ensure_binary(topic)
    except TypeError:
        errmsg = "Topic must be a string, not {}".format(topic, type(topic))
        raise EnvironmentError(errno.EINVAL, errmsg)

def modfind(modname):
    """Search FLUX_MODULE_PATH for a shared library (.so) of a given name

    :param modname: name of the module to search for
    :type modname: str, bytes, unicode
    :returns: path of the first matching shared library in FLUX_MODULE_PATH
    """
    searchpath = os.getenv("FLUX_MODULE_PATH")
    if searchpath is None:
        raise ValueError("FLUX_MODULE_PATH not set")
    modname = six.ensure_binary(modname)
    ret = raw.modfind(searchpath, modname, ffi.NULL, ffi.NULL)
    if ret is None:
        raise EnvironmentError(
            errno.ENOENT, "{} not found in module search path".format(modname)
        )
    return ret


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
            import traceback

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
