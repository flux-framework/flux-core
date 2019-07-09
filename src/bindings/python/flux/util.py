###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import sys
import errno
import json
import logging

import six

from flux.core.inner import ffi, raw

__all__ = ["check_future_error", "encode_payload", "encode_topic", "CLIMain"]


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
    elif isinstance(topic, six.text_type):
        topic = topic.encode("UTF-8")
    elif not isinstance(topic, six.binary_type):
        errmsg = "Topic must be a string, not {}".format(type(topic))
        raise TypeError(errno.EINVAL, errmsg)
    return topic


class CLIMain(object):
    def __init__(self, logger=None):
        if logger is None:
            self.logger = logging.getLogger()
        else:
            self.logger = logger

    def __call__(self, main_func):
        logging.basicConfig(
            level=logging.INFO, format="%(module)s: %(levelname)s: %(message)s"
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
