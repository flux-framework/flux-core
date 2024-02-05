#!/usr/bin/env python3
##############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import itertools
import math
import os
import sys
import time

import flux
import flux.uri
from flux.job import cancel_async
from flux.resource import resource_list


class InstanceBench:
    """Class representing a single Flux instance bootstrap benchmark"""

    def __init__(
        self,
        flux_handle,
        nnodes,
        brokers_per_node=1,
        topo="kary:2",
        conf=None,
        progress=None,
        exclusive=True,
    ):

        self.flux_handle = flux_handle
        self.nnodes = nnodes
        self.brokers_per_node = brokers_per_node
        self.topo = topo
        self.id = None
        self.t0 = None
        self.t_submit = None
        self.t_start = None
        self.t_uri = None
        self.t_shell_init = None
        self.t_ready = None
        self.t_finish = None
        self.child_handle = None
        self.then_cb = None
        self.then_args = []
        self.then_kw_args = {}
        self.progress = progress
        self.size = nnodes * brokers_per_node
        self.topo = topo
        self.name = f"[N:{nnodes:<4d} SZ:{self.size:<4d} {self.topo:<8}]"

        broker_opts = ["-Sbroker.rc2_none=1"]
        if topo is not None:
            broker_opts.append(f"-Stbon.topo={topo}")
        if conf is not None:
            broker_opts.append("-c{{tmpdir}}/conf.json")

        jobspec = flux.job.JobspecV1.from_command(
            command=["flux", "broker", *broker_opts],
            exclusive=exclusive,
            num_nodes=nnodes,
            num_tasks=nnodes * brokers_per_node,
        )
        jobspec.setattr_shell_option("mpi", "none")
        if conf is not None:
            jobspec.add_file("conf.json", conf)
        self.jobspec = jobspec

    def log(self, msg):
        try:
            ts = self.ts or (time.time() - self.t0)
        except AttributeError:
            ts = 0.0
        print(f"{self.name}: {ts:6.3f}s: {msg}", file=sys.stderr, flush=True)
        self.ts = None

    def then(self, cb, *args, **kw_args):
        self.then_cb = cb
        self.then_args = args
        self.then_kw_args = kw_args

    def submit(self):
        self.t0 = time.time()
        flux.job.submit_async(self.flux_handle, self.jobspec).then(self.submit_cb)
        return self

    def submit_cb(self, future):
        try:
            self.id = future.get_id()
        except OSError as exc:
            print(exc, file=sys.stderr)
            return
        if self.progress:
            job = flux.job.JobInfo(
                {
                    "id": self.id,
                    "state": flux.constants.FLUX_JOB_STATE_SCHED,
                    "t_submit": time.time(),
                }
            )
            self.progress.add_job(job)
        flux.job.event_watch_async(self.flux_handle, self.id).then(self.bg_wait_cb)

    def child_ready_cb(self, future):
        future.get()
        self.t_ready = time.time()
        self.log("ready")

        self.size = self.child_handle.attr_get("size")
        self.topo = self.child_handle.attr_get("tbon.topo")

        #  Shutdown and report timing:
        self.log("requesting shutdown")
        self.child_handle.rpc("shutdown.start", {"loglevel": 1})

    def bg_wait_cb(self, future):
        event = future.get_event()
        if self.progress:
            self.progress.process_event(self.id, event)
        if not event:
            #  The job has unexpectedly exited since we're at the end
            #   of the eventlog. Run `flux job attach` since this will dump
            #   any errors or output, then raise an exception.
            os.system(f"flux job attach {self.id} >&2")
            raise OSError(f"{self.id}: unexpectedly exited")

        self.ts = event.timestamp - self.t0
        # self.log(f"{event.name}")
        if event.name == "submit":
            self.t_submit = event.timestamp
        elif event.name == "alloc":
            self.t_alloc = event.timestamp
        elif event.name == "start":
            self.t_start = event.timestamp
            flux.job.event_watch_async(
                self.flux_handle, self.id, eventlog="guest.exec.eventlog"
            ).then(self.shell_init_wait_cb)
        elif event.name == "memo" and "uri" in event.context:
            self.t_uri = event.timestamp
            uri = str(flux.uri.JobURI(event.context["uri"]))
            self.log(f"opening handle to {self.id}")
            self.child_handle = flux.Flux(uri)

            #  Set main handle reactor as reactor for his child handle so
            #  events can be processed:
            self.child_handle.flux_set_reactor(self.flux_handle.get_reactor())

            self.log("connected to child job")

            #  Wait for child instance to be ready:
            self.child_handle.rpc("state-machine.wait").then(self.child_ready_cb)

        elif event.name == "finish":
            self.t_finish = event.timestamp
            future.cancel(stop=True)
            if self.then_cb is not None:
                self.then_cb(self, *self.then_args, **self.then_kw_wargs)
            if self.progress:
                # Notify ProgressBar that this job is done via a None event
                self.progress.process_event(self.id, None)

    def shell_init_wait_cb(self, future):
        event = future.get_event()
        if not event:
            return
        self.ts = event.timestamp - self.t0
        self.log(f"exec.{event.name}")
        if event.name == "shell.init":
            self.t_shell_init = event.timestamp
            future.cancel(stop=True)

    def timing_header(self, file=sys.stdout):
        print(
            "%5s %5s %8s %8s %8s %8s %8s %8s %8s"
            % (
                "NODES",
                "SIZE",
                "TOPO",
                "T_START",
                "T_URI",
                "T_INIT",
                "T_READY",
                "(TOTAL)",
                "T_SHUTDN",
            ),
            file=file,
        )

    def report_timing(self, file=sys.stdout):
        # Only report instances that got to the ready state
        if not self.t_ready:
            return

        # Avoid error if t_shutdown is None
        if not self.t_finish:
            t_shutdown = "       -"
        else:
            t_shutdown = f"{self.t_finish - self.t_ready:8.3f}"

        print(
            "%5s %5s %8s %8.3f %8.3f %8.3f %8.3f %8.3f %s"
            % (
                self.nnodes,
                self.size,
                self.topo,
                self.t_start - self.t_alloc,
                self.t_uri - self.t_start,
                self.t_shell_init - self.t_alloc,
                self.t_ready - self.t_shell_init,
                self.t_ready - self.t_alloc,
                t_shutdown,
            ),
            file=file,
        )


def generate_values(end):
    """
    Generate a list of powers of 2 (including `1` by default), up to and
    including `end`. If `end` is not a power of 2 insert it as the last
    element in list to ensure it is present.
    The list is returned in reverse order (largest values first)
    """
    stop = int(math.log2(end)) + 1
    values = [1 << i for i in range(stop)]
    if end not in values:
        values.append(end)
    values.reverse()
    return values


def parse_args():
    parser = argparse.ArgumentParser(
        prog="instance-timing", formatter_class=flux.util.help_formatter()
    )
    parser.add_argument(
        "-N",
        "--max-nodes",
        metavar="N",
        type=int,
        default=None,
        help="Scale up to N nodes by powers of two",
    )
    parser.add_argument(
        "-B",
        "--max-brokers-per-node",
        type=int,
        metavar="N",
        default=1,
        help="Run powers of 2 brokers-per-node up to N",
    )
    parser.add_argument(
        "--topo",
        metavar="TOPO,...",
        type=str,
        default="kary:2",
        help="add one or more tbon.topo values to test",
    )
    parser.add_argument(
        "--non-exclusive",
        action="store_true",
        help="Do not set exclusive flag on submitted jobs",
    )
    parser.add_argument(
        "-L",
        "--log-file",
        metavar="FILE",
        help="log results to FILE in addition to stdout",
    )
    return parser.parse_args()


def get_max_nnodes(flux_handle):
    """
    Get the maximum nodes available in the default queue or anonymous
    queue if there are no queues configured.
    """
    resources = resource_list(flux.Flux()).get()
    try:
        config = flux_handle.rpc("config.get").get()
        defaultq = config["policy"]["jobspec"]["defaults"]["system"]["queue"]
        constraint = config["queues"][defaultq]["requires"]
        avail = resources["up"].copy_constraint({"properties": constraint})
    except KeyError:
        avail = resources["up"]
    return avail.nnodes


def print_results(instances, ofile=sys.stdout):
    instances[0].timing_header(ofile)
    for ib in instances:
        ib.report_timing(ofile)


def try_cancel_all(handle, instances):
    n = 0
    for ib in instances:
        if not ib.t_finish:
            n += 1
            ib.log("canceling job")
            cancel_async(handle, ib.id)
    print(f"canceled {n} jobs", file=sys.stderr)


def main():
    args = parse_args()
    args.topo = args.topo.split(",")
    exclusive = not args.non_exclusive

    h = flux.Flux()
    if not args.max_nodes:
        args.max_nodes = get_max_nnodes(h)

    nnodes = generate_values(args.max_nodes)
    bpn = generate_values(args.max_brokers_per_node)

    inputs = list(itertools.product(nnodes, bpn, args.topo))
    inputs.sort(key=lambda x: x[0] * x[1])
    inputs.reverse()

    progress = flux.job.watcher.JobProgressBar(h)
    progress.start()
    instances = []
    for i in inputs:
        instances.append(
            InstanceBench(
                h,
                i[0],
                brokers_per_node=i[1],
                topo=i[2],
                progress=progress,
                exclusive=exclusive,
            ).submit()
        )

    try:
        h.reactor_run()
    except (KeyboardInterrupt, Exception):
        #  Cancel all remaining jobs and print available results instead
        #  of just exiting on exception
        try_cancel_all(h, instances)

    print_results(instances)
    if args.log_file:
        with open(args.log_file, "w") as ofile:
            print_results(instances, ofile=ofile)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
