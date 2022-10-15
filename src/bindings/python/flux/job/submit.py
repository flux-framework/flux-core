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

from _flux._core import ffi, lib
from flux import constants
from flux.future import Future
from flux.job import JobID
from flux.job._wrapper import _RAW as RAW
from flux.job.Jobspec import _convert_jobspec_arg_to_string
from flux.util import check_future_error


class SubmitFuture(Future):
    """Future subclass representing job IDs."""

    def get_id(self):
        """Return the job ID represented by this future."""
        return submit_get_id(self)


def submit_async(
    flux_handle,
    jobspec,
    urgency=lib.FLUX_JOB_URGENCY_DEFAULT,
    waitable=False,
    debug=False,
    pre_signed=False,
    novalidate=False,
):
    """Ask Flux to run a job, without waiting for a response

    Submit a job to Flux.  This method returns immediately with a
    Flux Future, which can be used obtain the job ID later.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobspec: jobspec defining the job request
    :type jobspec: Jobspec or its string encoding
    :param urgency: job urgency 0 (lowest) through 31 (highest)
        (default is 16).  Priorities 0 through 15 are restricted to
        the instance owner.
    :type urgency: int
    :param waitable: allow result to be fetched with job.wait()
        (default is False).  Waitable=True is restricted to the
        instance owner.
    :type waitable: bool
    :param debug: enable job manager debugging events to job eventlog
        (default is False)
    :type debug: bool
    :param pre_signed: jobspec argument is already signed
        (default is False)
    :type pre_signed: bool
    :param novalidate: jobspec does not need to be validated.
        (default is False) novalidate=True is restricted to the
        instance owner.
    :type novalidate: bool
    :returns: a Flux Future object for obtaining the assigned jobid
    :rtype: SubmitFuture
    """
    jobspec = _convert_jobspec_arg_to_string(jobspec)
    flags = 0
    if waitable:
        flags |= constants.FLUX_JOB_WAITABLE
    if debug:
        flags |= constants.FLUX_JOB_DEBUG
    if pre_signed:
        flags |= constants.FLUX_JOB_PRE_SIGNED
    if novalidate:
        flags |= constants.FLUX_JOB_NOVALIDATE
    future_handle = RAW.submit(flux_handle, jobspec, urgency, flags)
    return SubmitFuture(future_handle)


@check_future_error
def submit_get_id(future):
    """Get job ID from a Future returned by job.submit_async()

    Process a response to a Flux job submit request.  This method blocks
    until the response is received, then decodes the result to obtain
    the assigned job ID.

    :param future: a Flux future object returned by job.submit_async()
    :type future: Future
    :returns: job ID
    :rtype: JobID
    """
    if future is None or future == ffi.NULL:
        raise EnvironmentError(errno.EINVAL, "future must not be None/NULL")
    future.wait_for()  # ensure the future is fulfilled
    jobid = ffi.new("flux_jobid_t[1]")
    RAW.submit_get_id(future, jobid)
    return JobID(jobid[0])


def submit(
    flux_handle,
    jobspec,
    urgency=lib.FLUX_JOB_URGENCY_DEFAULT,
    waitable=False,
    debug=False,
    pre_signed=False,
):
    """Submit a job to Flux

    Ask Flux to run a job, blocking until a job ID is assigned.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobspec: jobspec defining the job request
    :type jobspec: Jobspec or its string encoding
    :param urgency: job urgency 0 (lowest) through 31 (highest)
        (default is 16).  Priorities 0 through 15 are restricted to
        the instance owner.
    :type urgency: int
    :param waitable: allow result to be fetched with job.wait()
        (default is False).  Waitable=true is restricted to the
        instance owner.
    :type waitable: bool
    :param debug: enable job manager debugging events to job eventlog
        (default is False)
    :type debug: bool
    :param pre_signed: jobspec argument is already signed
        (default is False)
    :type pre_signed: bool
    :returns: job ID
    :rtype: int
    """
    future = submit_async(flux_handle, jobspec, urgency, waitable, debug, pre_signed)
    return future.get_id()
