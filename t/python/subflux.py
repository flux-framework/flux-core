###############################################################
# Copyright 2014 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import os
import subprocess
import sys

script_dir = os.path.dirname(os.path.abspath(__file__))

# ported from sharness.d/01-setup.sh
srcdir = os.path.abspath(
    os.path.join(os.environ["srcdir"] if "srcdir" in os.environ else "", "..")
)
builddir = os.path.abspath(
    os.path.join(os.environ["builddir"] if "builddir" in os.environ else "", "..")
)
flux_exe = os.path.join(builddir, "src", "cmd", "flux")

sys.path.append(script_dir + "/tap")


#  Ignore -v, --verbose and --root options so that python test scripts
#   can absorb the same options as sharness tests. Later, something could
#   be done with these options, but for now they are dropped silently.
parser = argparse.ArgumentParser()
parser.add_argument("--debug", "-d", action="store_true")
parser.add_argument("--root", metavar="PATH", type=str)
args, remainder = parser.parse_known_args()

sys.argv[1:] = remainder


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
    command = [flux_exe, "start", "--test-size", str(size)]
    if personality != "full":
        for rc_num in [1, 3]:
            attr = "broker.rc{}_path".format(rc_num)
            if personality == "minimal":
                command.append("-S{}=".format(attr))
            else:
                path = "{}/t/rc/rc{}-{}".format(srcdir, rc_num, personality)
                command.append("-S{}={}".format(attr, path))
                if not is_exe(path):
                    print("cannot execute {}".format(path), file=sys.stderr)
                    sys.exit(1)

    command.extend([sys.executable, sys.argv[0]])

    p = subprocess.Popen(
        command, env=child_env, bufsize=-1, stdout=sys.stdout, stderr=sys.stderr
    )
    p.wait()
    if p.returncode > 0:
        sys.exit(p.returncode)
    elif p.returncode < 0:
        sys.exit(128 + -p.returncode)
    return False
