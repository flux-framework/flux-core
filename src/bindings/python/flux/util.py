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

import six

from flux.core.inner import ffi, raw

__all__ = ["check_future_error", "encode_payload", "encode_topic"]


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
