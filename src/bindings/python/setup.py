###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from setuptools import setup
import os

here = os.path.abspath(os.path.dirname(__file__))
cffi_dep = "cffi>=1.1"
setup(
    name="flux",
    version="0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.1a1",
    description="Bindings to the flux resource manager API",
    setup_requires=[cffi_dep],
    cffi_modules=["_flux/_core_build.py:ffi"],
    install_requires=[cffi_dep],
)
