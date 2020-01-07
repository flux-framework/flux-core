###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from _flux._core import ffi, lib
from flux.wrapper import Wrapper


class Core(Wrapper):
    """
    Generic Core wrapper, you probably do not want or need one of these.
    """

    def __init__(self):
        """Set up the wrapper interface for functions prefixed with flux_"""
        super(Core, self).__init__(ffi, lib, prefixes=["flux_", "FLUX_"])


# keeping this for compatibility
# pylint: disable=invalid-name
raw = Core()
