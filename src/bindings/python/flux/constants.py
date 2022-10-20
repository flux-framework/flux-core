###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""Global constants for the flux interface"""

import re
import sys

from _flux._core import lib

MOD = sys.modules[__name__]
# Inject enum/define names matching ^FLUX_[A-Z_]+$ into module
ALL_LIST = []
PATTERN = re.compile("^FLUX_[A-Z_]+")
for k in dir(lib):
    if PATTERN.match(k):
        setattr(MOD, k, getattr(lib, k))
        ALL_LIST.append(k)

__all__ = ALL_LIST
