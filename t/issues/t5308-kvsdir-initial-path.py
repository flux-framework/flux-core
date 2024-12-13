#!/usr/bin/env python3
#
#  Modifying a job data via object (KVSDir) returned via job_kvs()
#    works.
#
import sys
import flux
import flux.job
from flux.job import JobspecV1

handle = flux.Flux()
jobspec = JobspecV1.from_command(command=["true"], num_tasks=1, num_nodes=1)
jobid = flux.job.submit(handle, jobspec, waitable=True)
flux.job.wait(handle, jobid=jobid)
jobdir = flux.job.job_kvs(handle, jobid)
jobdir["foo"] = "bar"
jobdir.commit()

jobdir2 = flux.job.job_kvs(handle, jobid)
if jobdir2["foo"] != "bar":
    sys.exit(1)
