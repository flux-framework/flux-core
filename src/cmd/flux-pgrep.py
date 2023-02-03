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
import json
import logging
import os
import sre_constants
import sys
from itertools import islice
from pathlib import PurePath

import flux
from flux.job import JobInfoFormat, JobList
from flux.job.pgrep import PgrepConstraint, PgrepConstraintParser
from flux.util import UtilConfig

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
                "{id.f58:>12} ?:{queue:<8.8} {username:<8.8} {name:<10.10+} "
                "{status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "long": {
            "description": "Extended flux-pgrep format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} {username:<8.8} {name:<10.10+} "
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
        super().__init__(name="flux-jobs", initial_dict=initial_dict)

    def validate(self, path, config):
        """Validate a loaded flux-pgrep config file as dictionary"""
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")


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


class FilterActionSetUpdate(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        values = values.split(",")
        getattr(namespace, self.dest).update(values)


def parse_args():
    parser = argparse.ArgumentParser(
        prog=PROGRAM, formatter_class=flux.util.help_formatter()
    )
    parser.add_argument(
        "-a",
        action="store_true",
        help="Target jobs in all states"
        if PROGRAM == "flux-pgrep"
        else argparse.SUPPRESS,
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
        "--queue",
        type=str,
        metavar="QUEUE",
        help="Limit output to specific queue",
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
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print constraint JSON object and exit",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Print extra debugging information from syntax parser",
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
        # join expression quoting terms with whitespace:
        s = " ".join(f"'{w}'" if " " in w else w for w in args.expression)
        constraint = PgrepConstraintParser().parse(s, debug=args.debug)
        pgrep = PgrepConstraint(constraint)
        if args.dry_run:
            print(json.dumps(pgrep.dict()))
            sys.exit(0)
    except (ValueError, SyntaxError, TypeError, sre_constants.error) as exc:
        raise (exc)
        LOGGER.error(f"expression error: {exc}")
        sys.exit(2)

    jobs = list(islice(filter(pgrep.match, fetch_jobs(args, fh)), args.count))

    #  Exit with exitcode 1 when no jobs match
    if not jobs:
        sys.exit(1)

    if PROGRAM == "flux-pkill":
        pkill(fh, args, jobs)

    sformatter = JobInfoFormat(formatter.filter_empty(jobs))

    if args.format == "default":
        args.no_header = True

    sformatter.print_items(jobs, no_header=args.no_header)
