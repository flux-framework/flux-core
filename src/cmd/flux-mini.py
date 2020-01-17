##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

from __future__ import print_function

# pylint: disable=duplicate-code
import os
import sys
import logging
import argparse
import json

import flux
from flux import job
from flux.job import JobspecV1
from flux import util
from flux import debugged


class SubmitCmd:
    """
    SubmitCmd submits a job, displays the jobid on stdout, and returns.

    Usage: flux mini submit [OPTIONS] cmd ...
    """

    def __init__(self):
        self.parser = self.create_parser()

    # pylint: disable=no-self-use
    def create_parser(self):
        """
        Create parser with args for submit subcommand
        """
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument(
            "-N", "--nodes", type=int, metavar="N", help="Number of nodes to allocate"
        )
        parser.add_argument(
            "-n",
            "--ntasks",
            type=int,
            metavar="N",
            default=1,
            help="Number of tasks to start",
        )
        parser.add_argument(
            "-c",
            "--cores-per-task",
            type=int,
            metavar="N",
            default=1,
            help="Number of cores to allocate per task",
        )
        parser.add_argument(
            "-g",
            "--gpus-per-task",
            type=int,
            metavar="N",
            help="Number of GPUs to allocate per task",
        )
        parser.add_argument(
            "-t",
            "--time-limit",
            type=str,
            metavar="FSD",
            help="Time limit in Flux standard duration, e.g. 2d, 1.5h",
        )
        parser.add_argument(
            "--priority",
            help="Set job priority (0-31, default=16)",
            type=int,
            metavar="N",
            default=16,
        )
        parser.add_argument(
            "--job-name",
            type=str,
            help="Set an optional name for job to NAME",
            metavar="NAME",
        )
        parser.add_argument(
            "-o",
            "--setopt",
            action="append",
            help="Set shell option OPT. An optional value is supported with"
            + " OPT=VAL (default VAL=1) (multiple use OK)",
            metavar="OPT",
        )
        parser.add_argument(
            "--setattr",
            action="append",
            help="Set job attribute ATTR to VAL (multiple use OK)",
            metavar="ATTR=VAL",
        )
        parser.add_argument(
            "--input",
            type=str,
            help="Redirect job stdin from FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--output",
            type=str,
            help="Redirect job stdout to FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--error",
            type=str,
            help="Redirect job stderr to FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--label-io",
            action="store_true",
            help="Add rank labels to stdout, stderr lines",
        )
        parser.add_argument(
            "--flags",
            action="append",
            help="Set comma separated list of job submission flags. Possible "
            + "flags:  debug, waitable",
            metavar="FLAGS",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Don't actually submit job, just emit jobspec",
        )
        parser.add_argument(
            "--debug-emulate", action="store_true", help=argparse.SUPPRESS
        )
        parser.add_argument(
            "command", nargs=argparse.REMAINDER, help="Job command and arguments"
        )
        return parser

    # pylint: disable=too-many-branches,too-many-statements
    def submit(self, args):
        """
        Submit job, constructing jobspec from args.
        Returns jobid.
        """
        if not args.command:
            raise ValueError("job command and arguments are missing")

        jobspec = JobspecV1.from_command(
            args.command,
            num_tasks=args.ntasks,
            cores_per_task=args.cores_per_task,
            gpus_per_task=args.gpus_per_task,
            num_nodes=args.nodes,
        )
        jobspec.cwd = os.getcwd()
        jobspec.environment = dict(os.environ)
        if args.time_limit is not None:
            jobspec.duration = args.time_limit

        if args.job_name is not None:
            jobspec.setattr("system.job.name", args.job_name)

        if args.input is not None:
            jobspec.setattr_shell_option("input.stdin.type", "file")
            jobspec.setattr_shell_option("input.stdin.path", args.input)

        if args.output is not None:
            jobspec.setattr_shell_option("output.stdout.type", "file")
            jobspec.setattr_shell_option("output.stdout.path", args.output)
            if args.label_io:
                jobspec.setattr_shell_option("output.stdout.label", True)

        if args.error is not None:
            jobspec.setattr_shell_option("output.stderr.type", "file")
            jobspec.setattr_shell_option("output.stderr.path", args.error)
            if args.label_io:
                jobspec.setattr_shell_option("output.stderr.label", True)

        if args.setopt is not None:
            for keyval in args.setopt:
                # Split into key, val with a default for 1 if no val given:
                key, val = (keyval.split("=", 1) + [1])[:2]
                try:
                    val = json.loads(val)
                except (json.JSONDecodeError, TypeError):
                    pass
                jobspec.setattr_shell_option(key, val)

        if args.debug_emulate:
            debugged.set_mpir_being_debugged(1)

        if debugged.get_mpir_being_debugged() == 1:
            # if stop-tasks-in-exec is present, overwrite
            jobspec.setattr_shell_option("stop-tasks-in-exec", json.loads("1"))

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

        arg_debug = False
        arg_waitable = False
        if args.flags is not None:
            for tmp in args.flags:
                for flag in tmp.split(","):
                    if flag == "debug":
                        arg_debug = True
                    elif flag == "waitable":
                        arg_waitable = True
                    else:
                        raise ValueError("--flags: Unknown flag " + flag)

        if args.dry_run:
            print(jobspec.dumps(), file=sys.stdout)
            sys.exit(0)

        flux_handle = flux.Flux()
        return job.submit(
            flux_handle,
            jobspec.dumps(),
            priority=args.priority,
            waitable=arg_waitable,
            debug=arg_debug,
        )

    def main(self, args):
        jobid = self.submit(args)
        print(jobid, file=sys.stdout)

    def get_parser(self):
        return self.parser


class RunCmd(SubmitCmd):
    """
    RunCmd is identical to SubmitCmd, except it attaches the the job
    after submission.  Some additional options are added to modify the
    attach behavior.

    Usage: flux mini run [OPTIONS] cmd ...
    """

    def __init__(self):
        super().__init__()
        self.parser.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=0,
            help="Increase verbosity on stderr (multiple use OK)",
        )

    def main(self, args):
        jobid = self.submit(args)

        # Display job id on stderr if -v
        # N.B. we must flush sys.stderr due to the fact that it is buffered
        # when it points to a file, and os.execvp leaves it unflushed
        if args.verbose > 0:
            print("jobid:", jobid, file=sys.stderr)
            sys.stderr.flush()

        # Build args for flux job attach
        attach_args = ["flux-job", "attach"]
        if args.label_io:
            attach_args.append("--label-io")
        if args.verbose > 1:
            attach_args.append("--show-events")
        if args.verbose > 2:
            attach_args.append("--show-exec")
        if args.debug_emulate:
            attach_args.append("--debug-emulate")
        attach_args.append(str(jobid))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"] + ":" + os.environ["PATH"]
        os.execvp("flux-job", attach_args)


LOGGER = logging.getLogger("flux-mini")


@util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-mini")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # run
    run = RunCmd()
    mini_run_parser_sub = subparsers.add_parser(
        "run",
        parents=[run.get_parser()],
        help="run a job interactively",
        formatter_class=flux.util.help_formatter(),
    )
    mini_run_parser_sub.set_defaults(func=run.main)

    # submit
    submit = SubmitCmd()
    mini_submit_parser_sub = subparsers.add_parser(
        "submit",
        parents=[submit.get_parser()],
        help="enqueue a job",
        formatter_class=flux.util.help_formatter(),
    )
    mini_submit_parser_sub.set_defaults(func=submit.main)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
