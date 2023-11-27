##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import os
import sys
import time

import flux
import flux.job
from flux.cli import base
from flux.uri import JobURI


class AllocCmd(base.MiniCmd):
    def __init__(self, prog, usage=None, description=None):
        self.t0 = None
        super().__init__(prog, usage, description, exclude_io=True)
        base.add_batch_alloc_args(self.parser)
        self.parser.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=0,
            help="Increase verbosity on stderr (multiple use OK)",
        )
        self.parser.add_argument(
            "--bg",
            action="store_true",
            help="Wait for new instance to start, but do not attach to it.",
        )
        self.parser.add_argument(
            "COMMAND",
            nargs=argparse.REMAINDER,
            help="Set the initial COMMAND of new Flux instance."
            + "(default is an interactive shell)",
        )

    def init_jobspec(self, args):
        #  If number of slots not specified, then set it to node count
        #   if set, otherwise raise an error.
        if not args.nslots:
            if not args.nodes:
                raise ValueError("Number of slots to allocate must be specified")
            args.nslots = args.nodes
            args.exclusive = True

        #  For --bg, do not run an rc2 (initial program) unless
        #    the user explicitly specified COMMAND:
        if args.bg and not args.COMMAND:
            args.broker_opts = args.broker_opts or []
            args.broker_opts.append("-Sbroker.rc2_none=1")

        if args.dump:
            args.broker_opts = args.broker_opts or []
            args.broker_opts.append("-Scontent.dump=" + args.dump)

        jobspec = flux.job.JobspecV1.from_nest_command(
            command=args.COMMAND,
            num_slots=args.nslots,
            cores_per_slot=args.cores_per_slot,
            gpus_per_slot=args.gpus_per_slot,
            num_nodes=args.nodes,
            broker_opts=base.list_split(args.broker_opts),
            exclusive=args.exclusive,
            conf=args.conf.config,
        )

        #  For --bg, always allocate a pty, but not interactive,
        #   since an interactive pty causes the job shell to hang around
        #   until a pty client attaches, which may never happen.
        #
        #  O/w, allocate an interactive pty only if stdin is a tty
        #
        if args.bg:
            jobspec.setattr_shell_option("pty.capture", 1)
        elif sys.stdin.isatty():
            jobspec.setattr_shell_option("pty.interactive", 1)
        return jobspec

    @staticmethod
    def log(jobid, ts, msg):
        print(f"{jobid}: {ts:6.3f}s: {msg}", file=sys.stderr, flush=True)

    def bg_wait_cb(self, future, args, jobid):
        """
        Wait for memo event, connect to child instance, and finally wait
        for rc1 to complete
        """
        event = future.get_event()
        if not event:
            #  The job has unexpectedly exited since we're at the end
            #   of the eventlog. Run `flux job attach` since this will dump
            #   any errors or output, then raise an exception.
            os.system(f"flux job attach {jobid} >&2")
            raise OSError(f"{jobid}: unexpectedly exited")

        if not self.t0:
            self.t0 = event.timestamp
        ts = event.timestamp - self.t0

        if args.verbose and event.name == "alloc":
            self.log(jobid, ts, "resources allocated")
        if event.name == "memo" and "uri" in event.context:
            if args.verbose:
                self.log(jobid, ts, "waiting for instance")

            #  Wait for child instance to finish rc1 using state-machine.wait,
            #   then stop the reactor to return to caller.
            uri = str(JobURI(event.context["uri"]))
            try:
                child_handle = flux.Flux(uri)
            except OSError as exc:
                raise OSError(f"Unable to connect to {jobid}: {exc}")
            try:
                child_handle.rpc("state-machine.wait").get()
            except OSError:
                raise OSError(f"{jobid}: instance startup failed")

            if args.verbose:
                self.log(jobid, time.time() - self.t0, "ready")
            self.flux_handle.reactor_stop()

    def background(self, args, jobid):
        """Handle the --bg option

        Wait for child instance to be ready to accept jobs before returning.
        Print jobid to stdout once the job is ready.
        """
        jobid = flux.job.JobID(jobid)

        flux.job.event_watch_async(self.flux_handle, jobid).then(
            self.bg_wait_cb, args, jobid
        )
        if args.verbose:
            self.log(jobid, 0.0, "waiting for resources")
        try:
            self.flux_handle.reactor_run()
        except KeyboardInterrupt:
            print(f"\r{jobid}: Interrupt: canceling job", file=sys.stderr)
            flux.job.cancel(self.flux_handle, jobid)
            sys.exit(1)

        print(jobid)

    def main(self, args):
        jobid = self.submit(args)

        if args.bg:
            self.background(args, jobid)
            sys.exit(0)

        # Display job id on stderr if -v
        # N.B. we must flush sys.stderr due to the fact that it is buffered
        # when it points to a file, and os.execvp leaves it unflushed
        if args.verbose > 0:
            print("jobid:", jobid, file=sys.stderr)
            sys.stderr.flush()

        # Build args for flux job attach
        attach_args = ["flux-job", "attach"]
        attach_args.append(jobid.f58.encode("utf-8", errors="surrogateescape"))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        old_path = os.environ.get("PATH")
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"]
        if old_path:
            os.environ["PATH"] += f":{old_path}"

        os.execvp("flux-job", attach_args)
