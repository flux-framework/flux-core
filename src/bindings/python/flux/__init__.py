###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

"""
python bindings to flux-core, the main core of the flux resource manager
"""
from pkgutil import extend_path

__path__ = extend_path(__path__, __name__)

import flux.core.handle


# Manually lazy
# pylint: disable=invalid-name
def Flux(*args, **kwargs):
    return flux.core.handle.Flux(*args, **kwargs)


__all__ = ["core", "kvs", "rpc", "constants", "Flux"]
