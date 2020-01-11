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
fluxsrcdir = os.path.abspath(
    os.path.join(os.environ["FLUX_SRCDIR"] if "FLUX_SRCDIR" in os.environ else "", "..")
)
py2bdir = os.path.abspath(
    os.path.join(os.environ["builddir"] if "builddir" in os.environ else "", "..")
)
builddir = os.path.abspath(
    os.path.join(os.environ["FLUX_BUILDDIR"] if "FLUX_BUILDDIR" in os.environ else "", "..")
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
    command = [flux_exe, "start", "--bootstrap=selfpmi", "--size", str(size)]
    if personality != "full":
        for rc_num in [1, 3]:
            attr = "broker.rc{}_path".format(rc_num)
            if personality == "minimal":
                command.append("-o,-S{}=".format(attr))
            else:
                path = "{}/t/rc/rc{}-{}".format(fluxsrcdir, rc_num, personality)
                command.append("-o,-S{}={}".format(attr, path))
                if not is_exe(path):
                    print("cannot execute {}".format(path), file=sys.stderr)
                    sys.exit(1)

    new_pythonpath = [
      os.path.join(py2bdir),
      os.path.join(srcdir),
      os.path.join(srcdir, 't', 'python', 'tap'),
    ]

    new_pythonpath.extend(os.environ['PYTHONPATH'].split(':'))
    for n in new_pythonpath:
      print(n)
    command.extend(['env', 'PYTHONPATH={}'.format(':'.join(new_pythonpath)), sys.executable, sys.argv[0]])

    print(command)
    p = subprocess.Popen(
        command, env=child_env, bufsize=-1, stdout=sys.stdout, stderr=sys.stderr
    )
    p.wait()
    return False
