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
import logging
import os
import re
import sys
from itertools import islice
from pathlib import PurePath

import flux
from flux.job import JobID, JobInfoFormat, JobList
from flux.util import FilterActionSetUpdate, UtilConfig

PROGRAM = PurePath(sys.argv[0]).stem
LOGGER = logging.getLogger(PROGRAM)


class FluxPgrepConfig(UtilConfig):
    """flux-pgrep configuration class"""

    builtin_formats = {
        "default": {
            "description": "Default flux-pgrep format string",
            "format": "{id.f58}",
        },
        "full": {
            "description": "full flux-pgrep format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8} {name:<10.10+} "
                "{status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "long": {
            "description": "Extended flux-pgrep format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8} {name:<10.10+} "
                "{status:>9.9} {ntasks:>6} {nnodes:>6h} "
                "{t_submit!d:%b%d %R::>12} {t_remaining!F:>12h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "deps": {
            "description": "Show job urgency, priority, and dependencies",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} {name:<10.10+} {urgency:<3} "
                "{priority:<12} {state:<8.8} {dependencies}"
            ),
        },
    }

    def __init__(self):
        initial_dict = {"formats": dict(self.builtin_formats)}
        super().__init__(
            name="flux-jobs", toolname="flux-pgrep", initial_dict=initial_dict
        )

    def validate(self, path, config):
        """Validate a loaded flux-pgrep config file as dictionary"""
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")

    def get_default(self):
        return self.builtin_formats["default"]["format"]


class JobPgrep:
    def __init__(self, args):
        self.regex = None
        self.jobids = []
        for arg in args:
            if ":" not in arg and ".." in arg:
                #  X..Y with no : is translated to a range of jobids
                try:
                    self.jobids = list(map(JobID, arg.split("..")))
                    continue
                except ValueError:
                    # If terms cannot be translated to JobID, then fall
                    # back to trying as a pattern
                    pass
            if self.regex:
                raise ValueError("Only one pattern can be provided")
            if arg.startswith("name:"):
                arg = arg[5:]
            self.regex = re.compile(arg)

    def match(self, job):
        if self.regex and not self.regex.search(job.name):
            return False
        if self.jobids:
            if job.id > self.jobids[1] or job.id < self.jobids[0]:
                return False
        return True


def fetch_jobs(args, flux_handle=None):
    if not flux_handle:
        flux_handle = flux.Flux()

    if args.A:
        args.user = "all"
    if args.a:
        args.filter.update(["pending", "running", "inactive"])
    if not args.filter:
        args.filter = {"pending", "running"}

    jobs_rpc = JobList(
        flux_handle,
        filters=args.filter,
        user=args.user,
        max_entries=args.max_entries,
        queue=args.queue,
    )
    jobs = jobs_rpc.jobs()

    #  Print all errors accumulated in JobList RPC:
    try:
        for err in jobs_rpc.errors:
            print(err, file=sys.stderr)
    except EnvironmentError:
        # Ignore errors printing to stderr
        pass

    return jobs


def parse_args():
    parser = argparse.ArgumentParser(
        prog=PROGRAM, formatter_class=flux.util.help_formatter()
    )
    parser.add_argument(
        "-a",
        action="store_true",
        help=(
            "Target jobs in all states"
            if PROGRAM == "flux-pgrep"
            else argparse.SUPPRESS
        ),
    )
    parser.add_argument("-A", action="store_true", help="Target all users")
    parser.add_argument(
        "-u",
        "--user",
        type=str,
        metavar="[USERNAME|UID]",
        default=str(os.getuid()),
        help="Limit output to specific username or userid "
        '(Specify "all" for all users)',
    )
    parser.add_argument(
        "-f",
        "--filter",
        action=FilterActionSetUpdate,
        metavar="STATE|RESULT",
        default=set(),
        help="List jobs with specific job state or result",
    )
    parser.add_argument(
        "-q",
        "--queue",
        type=FilterActionSetUpdate,
        metavar="QUEUE,...",
        help="Limit output to specific queue or queues",
    )
    parser.add_argument(
        "-c",
        "--count",
        type=int,
        metavar="N",
        default=99999,
        help="Limit output to the first N matches",
    )
    parser.add_argument(
        "--max-entries",
        type=int,
        metavar="N",
        default=1000,
        help="Limit number of searched jobs to N entries (default: 1000)",
    )
    if PROGRAM == "flux-pgrep":
        parser.add_argument(
            "-n",
            "--no-header",
            action="store_true",
            help="Suppress printing of header line",
        )
        parser.add_argument(
            "-o",
            "--format",
            type=str,
            default="default",
            metavar="FORMAT",
            help="Specify output format using Python's string format syntax "
            + " or a defined format by name (use 'help' to get a list of names)",
        )
    else:
        parser.add_argument(
            "--wait", action="store_true", help="Wait for jobs to finish after cancel"
        )
    parser.add_argument(
        "expression",
        metavar="EXPRESSION",
        type=str,
        nargs="+",
        help="job name pattern or id range",
    )
    return parser.parse_args()


def pkill(fh, args, jobs):
    success = 0
    exitcode = 0

    def wait_cb(future, job):
        future.get_dict()

    def cancel_cb(future, args, job):
        nonlocal success, exitcode
        try:
            future.get()
            success = success + 1
            if args.wait:
                flux.job.result_async(fh, job.id).then(wait_cb, job)
        except OSError as exc:
            exitcode = 1
            LOGGER.error(f"{job.id}: cancel: {exc}")

    for job in jobs:
        flux.job.cancel_async(fh, job.id).then(cancel_cb, args, job)
    fh.reactor_run()
    LOGGER.info("Canceled %d job%s", success, "s" if success != 1 else "")
    sys.exit(exitcode)


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")
    args = parse_args()

    if PROGRAM == "flux-pgrep":
        #  Process format before fetching jobs in case user makes an
        #  error, or 'help' is specified
        fmt = FluxPgrepConfig().load().get_format_string(args.format)
        try:
            formatter = JobInfoFormat(fmt)
        except ValueError as err:
            raise ValueError("Error in user format: " + str(err))

    fh = flux.Flux()
    try:
        pgrep = JobPgrep(args.expression)
    except (ValueError, SyntaxError, TypeError, re.error) as exc:
        LOGGER.error(f"expression error: {exc}")
        sys.exit(2)

    jobs = list(islice(filter(pgrep.match, fetch_jobs(args, fh)), args.count))

    #  Exit with exitcode 1 when no jobs match
    if not jobs:
        sys.exit(1)

    if PROGRAM == "flux-pkill":
        pkill(fh, args, jobs)

    sformatter = JobInfoFormat(formatter.filter(jobs, no_header=args.no_header))

    # "default" can be overridden by environment variable, so check if
    # it's different than the builtin default
    if (
        args.format == "default"
        and FluxPgrepConfig().get_default() == sformatter.get_format(orig=True)
    ):
        args.no_header = True

    sformatter.print_items(jobs, no_header=args.no_header)
