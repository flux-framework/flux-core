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


def is_file(fpath):
    return os.path.isfile(fpath)


def sanitize_env(env):
    """Sanitize environment variables in subflux env that may affect tests"""
    sanitize = (
        "FLUX_SHELL_RC_PATH",
        "FLUX_RC_EXTRA",
        "FLUX_CONF_DIR",
        "FLUX_JOB_CC",
        "FLUX_F58_FORCE_ASCII",
        "FLUX_MODPROBE_PATH",
        "FLUX_URI_RESOLVE_LOCAL",
    )
    for var in list(env.keys()):
        if var.startswith(("PMI", "SLURM")) or var in sanitize:
            del env[var]


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
    child_env["FLUX_RC_USE_MODPROBE"] = "t"

    sanitize_env(child_env)

    command = [flux_exe, "start", "--test-size", str(size)]
    if personality != "full":
        rc1 = f"{srcdir}/t/rc/rc1-{personality}.py"
        rc3 = f"{srcdir}/t/rc/rc3.py"
        if not is_file(rc1):
            print(f"cannot run {rc1}", file=sys.stderr)
            sys.exit(1)
        command.append(f"-Sbroker.rc1_path=flux modprobe run {rc1}")
        command.append(f"-Sbroker.rc3_path=flux modprobe run {rc3}")

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
