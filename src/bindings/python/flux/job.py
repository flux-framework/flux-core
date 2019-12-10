###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import errno
import collections

import six

from flux.wrapper import Wrapper
from flux.util import check_future_error
from flux.future import Future
from _flux._core import ffi, lib


class JobWrapper(Wrapper):
    def __init__(self):
        super(JobWrapper, self).__init__(ffi, lib, prefixes=["flux_job_"])


RAW = JobWrapper()


def submit_async(flux_handle, jobspec, priority=lib.FLUX_JOB_PRIORITY_DEFAULT, flags=0):
    if isinstance(jobspec, six.text_type):
        jobspec = jobspec.encode("utf-8")
    elif jobspec is None or jobspec == ffi.NULL:
        # catch this here rather than in C for a better error message
        raise EnvironmentError(errno.EINVAL, "jobspec must not be None/NULL")
    elif not isinstance(jobspec, six.binary_type):
        raise TypeError("jobpsec must be a string (either binary or unicode)")

    future_handle = RAW.submit(flux_handle, jobspec, priority, flags)
    return Future(future_handle)


@check_future_error
def submit_get_id(future):
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.submit_get_id(future, jobid)
    return int(jobid[0])


def submit(flux_handle, jobspec, priority=lib.FLUX_JOB_PRIORITY_DEFAULT, flags=0):
    future = submit_async(flux_handle, jobspec, priority, flags)
    jid = submit_get_id(future)
    return jid


def wait_async(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    future_handle = RAW.wait(flux_handle, jobid)
    return Future(future_handle)


JobWaitResult = collections.namedtuple("JobWaitResult", "jobid, success, errstr")


@check_future_error
def wait_get_status(future):
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    success = ffi.new("bool[1]")
    errstr = ffi.new("const char *[1]")
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.wait_get_id(future, jobid)
    RAW.wait_get_status(future, success, errstr)
    return JobWaitResult(int(jobid[0]), bool(success[0]), ffi.string(errstr[0]))


def wait(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    future = wait_async(flux_handle, jobid)
    status = wait_get_status(future)
    return status
