#!/usr/bin/env python
import os
import sys
import subprocess

script_dir = os.path.dirname(os.path.abspath(__file__))

# copied from sharness.d/01-setup.sh
builddir = os.path.abspath(os.path.join(os.environ['builddir'] if 'builddir' in os.environ else "", ".."))
flux_exe = os.path.join(builddir, "src", "cmd", "flux")

def rerun_under_flux(size=1):
    try:
        if os.environ['IN_SUBFLUX'] == "1":
            return True
    except KeyError:
        pass

    child_env = dict(**os.environ)
    child_env['IN_SUBFLUX'] = "1"

    command = [flux_exe, 'start',
               '--bootstrap=selfpmi',
               '--size',str(size),
               sys.executable, sys.argv[0]]
    p = subprocess.Popen(command, env=child_env, bufsize=-1, stdout=sys.stdout, stderr=sys.stderr)
    p.wait()
    return False
