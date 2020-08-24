###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import errno
import collections

from flux.util import check_future_error
from flux.future import Future
from flux.job._wrapper import _RAW as RAW
from _flux._core import ffi, lib


class JobWaitFuture(Future):
    def get_status(self):
        return wait_get_status(self)


def wait_async(flux_handle, jobid=lib.FLUX_JOBID_ANY):
    """Wait for a job to complete, asynchronously

    Submit a request to wait for job completion.  This method returns
    immediately with a Flux Future, which can be used to process
    the result later.

    Only jobs submitted with waitable=True can be waited for.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID to wait for (default is any waitable job)
    :returns: a Flux Future object for obtaining the job result
    :rtype: Future
    """
    future_handle = RAW.wait(flux_handle, jobid)
    return JobWaitFuture(future_handle)


JobWaitResult = collections.namedtuple("JobWaitResult", "jobid, success, errstr")


@check_future_error
def wait_get_status(future):
    """Get job status from a Future returned by job.wait_async()

    Process a response to a Flux job wait request.  This method blocks
    until the response is received, then decodes the result to obtain
    the job status.

    :param future: a Flux future object returned by job.wait_async()
    :type future: Future
    :returns: job status, a tuple of: Job ID (int), success (bool),
        and an error (string) if success=False
    :rtype: tuple
    """
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
    """Wait for a job to complete

    Submit a request to wait for job completion, blocking until a
    response is received, then return the job status.

    Only jobs submitted with waitable=True can be waited for.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID to wait for (default is any waitable job)
    :returns: job status, a tuple of: Job ID (int), success (bool),
        and an error (string) if success=False
    :rtype: tuple
    """
    future = wait_async(flux_handle, jobid)
    return future.get_status()
