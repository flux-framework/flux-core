###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import flux
from _flux._core import ffi
from flux.job._wrapper import _RAW as RAW


def timeleft(flux_handle=None):
    """
    Return the remaining time in floating point seconds for the current
    job or enclosing instance.

    If the calling process is not associated with a job, then an exception
    will be raised. If the job associated with the current process has no
    timelimit, then ``float(inf)`` is returned.

    If a Flux handle is not provided, then this function will open a
    handle to the enclosing instance.
    """
    if flux_handle is None:
        flux_handle = flux.Flux()

    error = ffi.new("flux_error_t[1]")
    result = ffi.new("double[1]")
    try:
        RAW.timeleft(flux_handle, error, result)
    except OSError as err:
        raise OSError(err.errno, ffi.string(error[0].text).decode("utf-8")) from err
    return result[0]
