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
from typing import Optional, Union

from _flux._core import ffi, lib
from flux.core.handle import Flux  # for typing
from flux.future import Future
from flux.job._wrapper import _RAW as RAW
from flux.job.JobID import JobID  # for typing


def kill_async(
    flux_handle: Flux, jobid: Union[JobID, int], signum: Optional[int] = None
):
    """Send a signal to a running job asynchronously

    Args:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job to kill
        signum: signal to send (default SIGTERM)

    Returns:
        Future: a future fulfilled when the signal is delivered
    """
    if not signum:
        signum = signal.SIGTERM
    return Future(RAW.kill(flux_handle, int(jobid), signum))


def kill(flux_handle: Flux, jobid: Union[JobID, int], signum: Optional[int] = None):
    """Send a signal to a running job.

    Args:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job to kill
        signum: signal to send (default SIGTERM)
    """
    return kill_async(flux_handle, jobid, signum).get()


def cancel_async(
    flux_handle: Flux, jobid: Union[JobID, int], reason: Optional[str] = None
):
    """Cancel a pending or or running job asynchronously

    Arguments:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job to cancel
        reason: the textual reason associated with the cancelation

    Returns:
        Future: a future fulfilled when the cancelation completes
    """
    if not reason:
        reason = ffi.NULL
    return Future(RAW.cancel(flux_handle, int(jobid), reason))


def cancel(flux_handle: Flux, jobid: Union[JobID, int], reason: Optional[str] = None):
    """Cancel a pending or or running job

    Arguments:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job to cancel
        reason: the textual reason associated with the cancelation
    """
    return cancel_async(flux_handle, jobid, reason).get()


def job_raise_async(
    flux_handle: Flux,
    jobid: Union[JobID, int],
    exc_type: str,
    severity: int = 0,
    note: Optional[str] = None,
):
    """Raise a job exception asynchronously.

    Args:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job
        exc_type: exception type string (e.g. ``"scheduler-restart"``)
        severity: severity level; 0 causes the job to abort
        note: optional human-readable message

    Returns:
        Future: a future fulfilled when the exception is raised
    """
    note_enc = note.encode() if note else ffi.NULL
    return Future(
        lib.flux_job_raise(
            flux_handle.handle, int(jobid), exc_type.encode(), severity, note_enc
        )
    )


def job_raise(
    flux_handle: Flux,
    jobid: Union[JobID, int],
    exc_type: str,
    severity: int = 0,
    note: Optional[str] = None,
):
    """Raise a job exception.

    Args:
        flux_handle: handle for Flux broker from flux.Flux()
        jobid: the job ID of the job
        exc_type: exception type string (e.g. ``"scheduler-restart"``)
        severity: severity level; 0 causes the job to abort
        note: optional human-readable message
    """
    return job_raise_async(flux_handle, jobid, exc_type, severity, note).get()
