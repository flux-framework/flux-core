#!/usr/bin/env python

###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

from __future__ import print_function

import os
import sys
import subprocess

script_dir = os.path.dirname(os.path.abspath(__file__))

# ported from sharness.d/01-setup.sh
srcdir = os.path.abspath(
    os.path.join(os.environ["srcdir"] if "srcdir" in os.environ else "", "..")
)
builddir = os.path.abspath(
    os.path.join(os.environ["builddir"] if "builddir" in os.environ else "", "..")
)
flux_exe = os.path.join(builddir, "src", "cmd", "flux")


def is_exe(fpath):
    return os.path.isfile(fpath) and os.access(fpath, os.X_OK)


def rerun_under_flux(size=1, personality="full"):
    try:
        if os.environ["IN_SUBFLUX"] == "1":
            return True
    except KeyError:
        pass

    child_env = dict(**os.environ)
    child_env["IN_SUBFLUX"] = "1"

    # ported from sharness.d/flux-sharness.sh
    child_env["FLUX_BUILD_DIR"] = builddir
    child_env["FLUX_SOURCE_DIR"] = srcdir
    for rc_num in [1, 3]:
        env_var = "FLUX_RC{}_PATH".format(rc_num)
        if personality == "full":
            if env_var in child_env:
                del child_env[env_var]
        elif personality == "minimal":
            child_env[env_var] = ""
        else:
            path = "{}/t/rc/rc{}-{}".format(srcdir, rc_num, personality)
            child_env[env_var] = path
            if not is_exe(path):
                print("cannot execute {}".format(path), file=sys.stderr)
                sys.exit(1)

    command = [
        flux_exe,
        "start",
        "--bootstrap=selfpmi",
        "--size",
        str(size),
        sys.executable,
        sys.argv[0],
    ]
    p = subprocess.Popen(
        command, env=child_env, bufsize=-1, stdout=sys.stdout, stderr=sys.stderr
    )
    p.wait()
    return False
