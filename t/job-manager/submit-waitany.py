###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: flux python submit-wait.py njobs
#
# Submit njobs jobs in a loop, then job.wait() njobs times
#

import flux
from flux import job
import sys
import subprocess


# Return jobspec for a simple job
def make_jobspec(cmd):
    out = subprocess.Popen(
        ["flux", "jobspec", "srun", cmd],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    stdout, stderr = out.communicate()
    return stdout


if len(sys.argv) != 2:
    njobs = 10
else:
    njobs = int(sys.argv[1])

# Open connection to broker
h = flux.Flux()

# Submit njobs test jobs (half will fail)
jobspec = make_jobspec("/bin/true")
jobspec_fail = make_jobspec("/bin/false")
flags = flux.constants.FLUX_JOB_WAITABLE
for i in range(njobs):
    if i < njobs / 2:
        jobid = job.submit(h, jobspec, flags=flags)
        print("submit: {} /bin/true".format(jobid))
    else:
        jobid = job.submit(h, jobspec_fail, flags=flags)
        print("submit: {} /bin/false".format(jobid))


# Wait for njobs jobs
for i in range(njobs):
    result = job.wait(h)
    if result.success:
        print("wait: {} Success".format(result.jobid))
    else:
        print("wait: {} Error: {}".format(result.jobid, result.errstr))

# vim: tabstop=4 shiftwidth=4 expandtab
