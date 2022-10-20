###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import importlib

from _flux._core import lib
from flux.core.handle import Flux


def mod_main_trampoline(name, int_handle, args):
    # generate a flux wrapper class instance from the handle
    flux_instance = Flux(handle=lib.unpack_long(int_handle))
    # increment reference count to prevent destruction of the underlying handle
    # (which is owned by the broker) when the flux handle leaves scope and is
    # garbage collected
    flux_instance.incref()
    user_mod = None
    try:
        user_mod = importlib.import_module("flux.modules." + name)
    except ImportError:  # check user paths for the module
        user_mod = importlib.import_module(name)

    # call into mod_main with a flux class instance and the argument dict
    # it might be more pythonic to unpack the args as keyword/positional
    # arguments to this function, but I think this is cleaner for now
    user_mod.mod_main(flux_instance, *args)
