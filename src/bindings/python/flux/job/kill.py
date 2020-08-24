###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################
import signal

from flux.future import Future
from flux.job._wrapper import _RAW as RAW
from _flux._core import ffi


def kill_async(flux_handle, jobid, signum=None):
    """Send a signal to a running job asynchronously

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to kill
    :param signum: signal to send (default SIGTERM)
    :returns: a Future
    :rtype: Future
    """
    if not signum:
        signum = signal.SIGTERM
    return Future(RAW.kill(flux_handle, int(jobid), signum))


def kill(flux_handle, jobid, signum=None):
    """Send a signal to a running job.

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to kill
    :param signum: signal to send (default SIGTERM)
    """
    return kill_async(flux_handle, jobid, signum).get()


def cancel_async(flux_handle, jobid, reason=None):
    """Cancel a pending or or running job asynchronously

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to cancel
    :returns: a Future
    :rtype: Future

    """
    if not reason:
        reason = ffi.NULL
    return Future(RAW.cancel(flux_handle, int(jobid), reason))


def cancel(flux_handle, jobid, signum=None):
    """Cancel a pending or or running job

    :param flux_handle: handle for Flux broker from flux.Flux()
    :type flux_handle: Flux
    :param jobid: the job ID of the job to cancel

    """
    return cancel_async(flux_handle, jobid, signum).get()
