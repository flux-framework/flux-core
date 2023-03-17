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
# Submit njobs jobs in a loop, then wait for them by id.
#

import flux
from flux import job
from flux.job import JobspecV1
import sys

if len(sys.argv) != 2:
    njobs = 10
else:
    njobs = int(sys.argv[1])

# Open connection to broker
h = flux.Flux()

# Submit njobs test jobs (half will fail)
jobspec = JobspecV1.from_command(["true"])
jobspec_fail = JobspecV1.from_command(["false"])
jobs = []
for i in range(njobs):
    if i < njobs / 2:
        jobid = job.submit(h, jobspec, waitable=True)
        print("submit: {} true".format(jobid))
    else:
        jobid = job.submit(h, jobspec_fail, waitable=True)
        print("submit: {} false".format(jobid))
    jobs.append(jobid)

# Wait for each job in turn
for jobid in jobs:
    result = job.wait(h, jobid)
    if result.success:
        print("wait: {} Success".format(result.jobid))
    else:
        print("wait: {} Error: {}".format(result.jobid, result.errstr))

# vim: tabstop=4 shiftwidth=4 expandtab
