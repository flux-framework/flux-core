###############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from _flux._core import lib


def get_mpir_being_debugged():
    return lib.get_mpir_being_debugged()


def set_mpir_being_debugged(value):
    return lib.set_mpir_being_debugged(value)
