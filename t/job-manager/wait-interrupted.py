###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: flux python wait-interrupted.py njobs
#
# Submit njobs jobs in a loop, then wait for them asynchronously.
# Exit before the waits complete, thus triggering disconnect cleanup.
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

# Submit njobs test jobs
jobspec = JobspecV1.from_command(["true"])
jobs = []
for i in range(njobs):
    jobid = job.submit(h, jobspec, waitable=True)
    print("submit: {} true".format(jobid))
    jobs.append(jobid)

# Async wait which we immediately abandon
# Do half with jobid, half without to cover both disconnect loops
# N.B. most likely this leaves some zombies so clean up after
for i in range(njobs):
    if i < njobs / 2:
        f = job.wait_async(h, jobs[i])
    else:
        f = job.wait_async(h)

# vim: tabstop=4 shiftwidth=4 expandtab
