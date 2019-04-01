#!/usr/bin/env python

###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: bulk-state.py njobs
#
# Submit njobs jobs while watching job state notification events.
# Ensure that each job progresses through an expected set of states,
# in order.
#

import flux
from flux import job
import sys
import subprocess

expected_states = ["NEW", "DEPEND", "SCHED", "RUN", "CLEANUP", "INACTIVE"]

# Return jobspec for a simple job
def make_jobspec():
    out = subprocess.Popen(
        ["flux", "jobspec", "srun", "hostname"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    stdout, stderr = out.communicate()
    return stdout


# Return True if all jobs in the jobs dictionary have reached 'INACTIVE' state
def all_inactive(jobs):
    for jobid in jobs:
        states = jobs[jobid]
        if states[-1] != "INACTIVE":
            return False
    return True


# For each job mentioned in a job-state event message, append a
# new state to its entry in the jobs dictionary.
# Ignore state notifications for jobs that are not already in the dict
def parse_notification(jobs, msg):
    for entry in msg.payload["transitions"]:
        jobid = entry[0]
        state = entry[1]
        if jobid in jobs:
            jobs[jobid].append(state)


if len(sys.argv) != 2:
    njobs = 10
else:
    njobs = int(sys.argv[1])

# Open connection to broker and subscribe to state notifications
h = flux.Flux()
h.event_subscribe("job-state")

# Submit several test jobs, building dictionary by jobid,
# where each entry contains a list of job states
# N.B. no notification is provided for the NEW state
jobspec = make_jobspec()
jobs = {}
for i in range(njobs):
    jobid = job.submit(h, jobspec)
    jobs[jobid] = ["NEW"]

# Process events until all jobs have reached INACTIVE state.
while not all_inactive(jobs):
    event = h.event_recv()
    parse_notification(jobs, event)

# Verify that each job advanced through the expected set of states, in order
for jobid in jobs:
    if cmp(jobs[jobid], expected_states) != 0:
        print("{}: {}: {}".format("bad state list", jobid, jobs[jobid]))
        sys.exit(1)

# Unsubscribe to state notifications and close connection to broker.
h.event_unsubscribe("job-state")
h.close()

# vim: tabstop=4 shiftwidth=4 expandtab
