##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import ctypes
import ctypes.util
import platform


def set_proctitle(name):
    """Set the process name shown in top, ps, and /proc/self/comm.

    Uses prctl(PR_SET_NAME) on Linux, matching the mechanism used by the
    Flux broker itself. The kernel silently truncates the name to 15
    characters. On non-Linux platforms this is a no-op.
    """
    if platform.system() != "Linux":
        return
    try:
        libc = ctypes.CDLL(ctypes.util.find_library("c"), use_errno=True)
        libc.prctl(15, name.encode(), 0, 0, 0)  # 15 == PR_SET_NAME
    except Exception:  # pylint: disable=broad-except
        pass


# vi: ts=4 sw=4 expandtab
