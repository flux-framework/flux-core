###############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# Usage: flux python list-id.py njobs
#
# Submit njobs jobs in a loop and immediately get job info for it
#

import flux
from flux import job
import sys

if len(sys.argv) != 2:
    njobs = 100
else:
    njobs = int(sys.argv[1])

attrs = ["state", "name", "t_submit"]
jobspec = job.JobspecV1.from_command(["sleep", "0"], num_tasks=1, cores_per_task=1)


def list_cb(f):
    print(f.get())


def submit_cb(f):
    jobid = job.submit_get_id(f)
    h.rpc("job-list.list-id", dict(id=jobid, attrs=attrs)).then(list_cb)


h = flux.Flux()
for i in range(njobs):
    job.submit_async(h, jobspec).then(submit_cb)

if h.reactor_run() < 0:
    h.fatal_error("reactor_run failed")

# vim: tabstop=4 shiftwidth=4 expandtab
