###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import collections
import errno
import json

import flux
from _flux._core import ffi, lib
from flux.future import Future
from flux.job._wrapper import _RAW as RAW
from flux.util import check_future_error, interruptible


class JobWaitFuture(Future):
    def get_status(self):
        """Return the result of a job wait request.

        This method blocks until the response is received,
        then decodes the result to obtain the job status.

        :returns: job status, a tuple of: Job ID (int), success (bool),
            and an error (string) if success=False
        :rtype: JobWaitResult
        """
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
    :rtype: JobWaitFuture
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
    :rtype: JobWaitResult
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
    :rtype: JobWaitResult
    """
    future = wait_async(flux_handle, jobid)
    return future.get_status()


class JobResultFuture(Future):
    """Future fulfilled with a job "result"

    Supports methods to return the result as either a raw :obj:`dict` or
    :obj:`flux.job.info.JobInfo` object.
    """

    @interruptible
    def get_dict(self):
        """Get the raw "result" dictionary for the job

        Return the underlying "result" payload from ``flux_job_result(3)``
        as a dictionary.
        """
        try:
            result_str = ffi.new("char *[1]")
            RAW.result_get(self.handle, result_str)
        except OSError:
            self.raise_if_handle_exception()
            raise
        if result_str[0] == ffi.NULL:
            return None
        return json.loads(ffi.string(result_str[0]).decode("utf-8"))

    @interruptible
    def get_info(self):
        """Get a :obj:`flux.job.info.JobInfo` object from the job result

        Note: The JobInfo object returned from this method is only capable
        of computing a small subset of job information, including, but possibly
        not limited to:
            - id
            - t_submit, t_run, t_cleanup
            - returncode
            - waitstatus
            - runtime
            - result
            - result_id
        """
        return flux.job.JobInfo(self.get_dict())


def result_async(flux_handle, jobid, flags=0):
    """Wait for a job to reach its terminal state and return job result

    This function waits for job completion by watching the eventlog.
    Because this function must process the eventlog, it is a little more
    heavyweight than :meth:`flux.job.wait.wait_async`. However, it may
    be used for non-waitable jobs, jobs that have already completed,
    and works multiple times on the same jobid.

    Once the eventlog terminal state is reached, the returned Future is
    fulfilled with a set of information gleaned from the processed events,
    including whether the job started running (in case it was canceled
    before starting), any exception state, and the final exit code and
    wait(2) status.

    Args:
        flux_handle (:obj:`flux.Flux`): handle for Flux broker
        jobid (:obj:`flux.job.JobID`): the jobid for which to fetch result

    Returns:
        JobResultFuture: A Future fulfilled with the job result.
    """
    future = RAW.result(flux_handle, flux.job.JobID(jobid), flags)
    return JobResultFuture(future)


def result(flux_handle, jobid, flags=0):
    """Wait for a job to reach its terminal state and return job result

    This function waits for job completion by watching the eventlog.
    Because this function must process the eventlog, it is a little more
    heavyweight than :meth:`flux.job.wait.wait`. However, it may be used
    for non-waitable jobs, jobs that have already completed, and works
    multiple times on the same jobid.

    This function will wait until the job result is available and returns
    a :obj:`flux.job.info.JobInfo` object filled with the available information.

    Note: The JobInfo object returned from this method is only capable
    of computing a small subset of job information, including, but possibly
    not limited to:
        - id
        - t_submit, t_run, t_cleanup
        - returncode
        - waitstatus
        - runtime
        - result
        - result_id

    Args:
        flux_handle (:obj:`flux.Flux`): handle for Flux broker
        jobid (:obj:`flux.job.JobID`): the jobid for which to fetch result

    Returns:
        JobInfo: A limited JobInfo object which can be used to fetch
        the final job result, returncode, etc.
    """
    future = result_async(flux_handle, flux.job.JobID(jobid), flags)
    return future.get_info()
