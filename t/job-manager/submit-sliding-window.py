###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: flux python submit-sliding-window.py njobs fanout
#
# Run 'njobs' jobs, keeping at most 'fanout' active at once
#

import flux
from flux import job
from flux.job import JobspecV1
import sys


njobs = 10
fanout = 2
if len(sys.argv) >= 2:
    njobs = int(sys.argv[1])
if len(sys.argv) == 3:
    fanout = int(sys.argv[2])

# Open connection to broker
h = flux.Flux()

jobspec = JobspecV1.from_command(["true"])
done = 0
running = 0

while done < njobs:
    if running < fanout and done + running < njobs:
        jobid = job.submit(h, jobspec, waitable=True)
        print("submit: {}".format(jobid))
        running += 1

    if running == fanout or done + running == njobs:
        jobid, success, errstr = job.wait(h)
        if success:
            print("wait: {} Success".format(jobid))
        else:
            print("wait: {} Error: {}".format(jobid, errstr))
        done += 1
        running -= 1

# vim: tabstop=4 shiftwidth=4 expandtab
