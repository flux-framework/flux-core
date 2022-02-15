#!/usr/bin/env python3
##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import sys
import time
import argparse
import json

import flux
from flux import job
from flux.job import JobspecV1
from flux.progress import Bottombar


def parse_args():
    parser = argparse.ArgumentParser(description="Run job throughput test")
    parser.add_argument(
        "-n",
        "--njobs",
        type=int,
        metavar="N",
        help="Set the total number of jobs to run",
        default=100,
    )
    parser.add_argument(
        "-t",
        "--runtime",
        help="When simulating execution, runtime of each job (default=1ms)",
        default="0.001s",
    )
    parser.add_argument(
        "-x",
        "--exec",
        help="Do not simulate execution, actually run jobs",
        action="store_true",
    )
    parser.add_argument(
        "-o",
        "--setopt",
        action="append",
        help="Set shell option OPT or OPT=VAL (multiple use OK)",
        metavar="OPT",
    )
    parser.add_argument(
        "--setattr",
        action="append",
        help="Set job attribute ATTR to VAL (multiple use OK)",
        metavar="ATTR=VAL",
    )
    parser.add_argument(
        "-s",
        "--status",
        help="Add a status bar to bottom of terminal",
        action="store_true",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        help="Log job events",
        action="store_true",
    )
    parser.add_argument("command", nargs=argparse.REMAINDER, default=["true"])

    return parser.parse_args()


def create_test_jobspec(args):

    #  Create a test jobspec
    if not args.command:
        args.command = ["true"]
    jobspec = JobspecV1.from_command(args.command)

    #  Set any requested shell options
    if args.setopt is not None:
        for keyval in args.setopt:
            # Split into key, val with a default for 1 if no val given:
            key, val = (keyval.split("=", 1) + [1])[:2]
            try:
                val = json.loads(val)
            except (json.JSONDecodeError, TypeError):
                pass
            jobspec.setattr_shell_option(key, val)

    #  Set any requested Jobspec attributes
    if args.setattr is not None:
        for keyval in args.setattr:
            tmp = keyval.split("=", 1)
            if len(tmp) != 2:
                raise ValueError("--setattr: Missing value for attr " + keyval)
            key = tmp[0]
            try:
                val = json.loads(tmp[1])
            except (json.JSONDecodeError, TypeError):
                val = tmp[1]
            jobspec.setattr(key, val)

    if not args.exec:
        jobspec.setattr("system.exec.test.run_duration", args.runtime)

    return jobspec


class BulkRun:

    # pylint: disable=too-many-instance-attributes

    def __init__(self, handle, total, jobspec):
        self.handle = handle
        self.total = total
        self.jobspec = jobspec
        self.jobs = {}
        self.submitted = 0
        self.running = 0
        self.complete = 0
        self.bbar = None

    def statusline(self, _bbar, _width):
        return (
            f"{self.total:>6} jobs: {self.submitted:>6} submitted, "
            f"{self.running:>6} running, {self.complete} completed"
        )

    def event_cb(self, future, args, jobid):
        event = future.get_event()
        if event is not None:
            if args.verbose:
                print(f"{jobid}: {event.name}")
            if event.name == "submit":
                self.submitted += 1
                self.jobs[jobid][event.name] = event
            elif event.name == "start":
                self.running += 1
            elif event.name == "finish":
                self.running -= 1
                self.complete += 1
            elif event.name == "clean":
                self.jobs[jobid][event.name] = event
            if self.bbar:
                self.bbar.update()

    def handle_submit(self, args, jobid):
        self.jobs[jobid] = {"t_submit": time.time()}
        fut = job.event_watch_async(self.handle, jobid)
        fut.then(self.event_cb, args, jobid)

    def submit_cb(self, future, args):
        # pylint: disable=broad-except
        try:
            self.handle_submit(args, future.get_id())
        except Exception as exc:
            print(f"Submission failed: {exc}", file=sys.stderr)

    def submit_async(self, args):
        spec = self.jobspec.dumps()
        for _ in range(args.njobs):
            job.submit_async(self.handle, spec).then(self.submit_cb, args)

    def run(self, args):
        if args.status:
            self.bbar = Bottombar(self.statusline).start()

        self.submit_async(args)

        self.handle.reactor_run()

        if self.bbar:
            self.bbar.stop()

        return self


def main():

    args = parse_args()

    time0 = time.time()

    jobspec = create_test_jobspec(args)

    bulk = BulkRun(flux.Flux(), args.njobs, jobspec).run(args)

    jobs = bulk.jobs

    #  Get the job with the earliest 'submit' event:
    first = jobs[min(jobs.keys(), key=lambda x: jobs[x]["submit"].timestamp)]

    #  Get the job with the latest 'clean' event:
    last = jobs[max(jobs.keys(), key=lambda x: jobs[x]["clean"].timestamp)]

    #  Get the job with the latest 't_submit' time:
    lastsubmit = jobs[max(jobs.keys(), key=lambda x: jobs[x]["t_submit"])]
    submit_time = lastsubmit["t_submit"] - time0
    sjps = args.njobs / submit_time

    script_runtime = time.time() - time0
    job_runtime = last["clean"].timestamp - first["submit"].timestamp
    jps = args.njobs / job_runtime
    jpsb = args.njobs / script_runtime

    print(f"number of jobs: {args.njobs}")
    print(f"submit time:    {submit_time:<6.3f}s ({sjps:5.1f} job/s)")
    print(f"script runtime: {script_runtime:<6.3f}s")
    print(f"job runtime:    {job_runtime:<6.3f}s")
    print(f"throughput:     {jps:<.1f} job/s (script: {jpsb:5.1f} job/s)")


if __name__ == "__main__":
    main()
