##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import os
import sys
import logging
import argparse
import fileinput
import json

import flux.constants
import flux.util
from flux.job import JobInfo, JobInfoFormat, JobList
from flux.job import JobID

LOGGER = logging.getLogger("flux-jobs")


def fetch_jobs_stdin():
    """
    Return a list of jobs gathered from a series of JSON objects, one per
    line, presented on stdin. This function is used for testing of the
    flux-jobs utility, and thus, all filtering options are currently
    ignored.
    """
    jobs = []
    for line in fileinput.input("-"):
        try:
            job = JobInfo(json.loads(line))
        except ValueError as err:
            LOGGER.error("JSON input error: line %d: %s", fileinput.lineno(), str(err))
            sys.exit(1)
        jobs.append(job)
    return jobs


def fetch_jobs_flux(args, fields):
    flux_handle = flux.Flux()

    # Note there is no attr for "id", its always returned
    fields2attrs = {
        "id": (),
        "id.dec": (),
        "id.hex": (),
        "id.f58": (),
        "id.kvs": (),
        "id.words": (),
        "id.dothex": (),
        "userid": ("userid",),
        "username": ("userid",),
        "urgency": ("urgency",),
        "state": ("state",),
        "state_single": ("state",),
        "name": ("name",),
        "ntasks": ("ntasks",),
        "nnodes": ("nnodes",),
        "ranks": ("ranks",),
        "nodelist": ("nodelist",),
        "success": ("success",),
        "exception.occurred": ("exception_occurred",),
        "exception.severity": ("exception_severity",),
        "exception.type": ("exception_type",),
        "exception.note": ("exception_note",),
        "result": ("result",),
        "result_abbrev": ("result",),
        "t_submit": ("t_submit",),
        "t_depend": ("t_depend",),
        "t_priority": ("t_priority",),
        "t_sched": ("t_sched",),
        "t_run": ("t_run",),
        "t_cleanup": ("t_cleanup",),
        "t_inactive": ("t_inactive",),
        "runtime": ("t_run", "t_cleanup"),
        "status": ("state", "result"),
        "status_abbrev": ("state", "result"),
        "expiration": ("expiration", "state", "result"),
        "t_remaining": ("expiration", "state", "result"),
        "annotations": ("annotations",),
        # Special cases, pointers to sub-dicts in annotations
        "sched": ("annotations",),
        "user": ("annotations",),
    }

    attrs = set()
    for field in fields:
        # Special case for annotations, can be arbitrary field names determined
        # by scheduler/user.
        if (
            field.startswith("annotations.")
            or field.startswith("sched.")
            or field.startswith("user.")
        ):
            attrs.update(fields2attrs["annotations"])
        else:
            attrs.update(fields2attrs[field])

    if args.color == "always" or args.color == "auto":
        attrs.update(fields2attrs["result"])

    if args.a:
        args.user = str(os.getuid())
    if args.A:
        args.user = str(flux.constants.FLUX_USERID_UNKNOWN)
    if args.a or args.A:
        args.filter = "pending,running,inactive"

    jobs_rpc = JobList(
        flux_handle,
        ids=args.jobids,
        attrs=attrs,
        filters=[args.filter],
        user=args.user,
        max_entries=args.count,
    )

    jobs = jobs_rpc.jobs()

    #  Print all errors accumulated in JobList RPC:
    try:
        for err in jobs_rpc.errors:
            print(err, file=sys.stderr)
    except EnvironmentError:
        pass

    return jobs


def fetch_jobs(args, fields):
    """
    Fetch jobs from flux or optionally stdin.
    Returns a list of JobInfo objects
    """
    if args.from_stdin:
        lst = fetch_jobs_stdin()
    else:
        lst = fetch_jobs_flux(args, fields)
    return lst


class FilterAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values)
        setattr(namespace, "filtered", True)


# pylint: disable=redefined-builtin
class FilterTrueAction(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest,
        const=True,
        default=False,
        required=False,
        help=None,
        metavar=None,
    ):
        super(FilterTrueAction, self).__init__(
            option_strings=option_strings,
            dest=dest,
            nargs=0,
            const=const,
            default=default,
            help=help,
        )

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, self.const)
        setattr(namespace, "filtered", True)


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-jobs", formatter_class=flux.util.help_formatter()
    )
    # -a equivalent to -s "pending,running,inactive" and -u set to userid
    parser.add_argument(
        "-a", action=FilterTrueAction, help="List all jobs for current user"
    )
    # -A equivalent to -s "pending,running,inactive" and -u set to "all"
    parser.add_argument(
        "-A", action=FilterTrueAction, help="List all jobs for all users"
    )
    parser.add_argument(
        "-c",
        "--count",
        action=FilterAction,
        type=int,
        metavar="N",
        default=1000,
        help="Limit output to N jobs(default 1000)",
    )
    parser.add_argument(
        "-f",
        "--filter",
        action=FilterAction,
        type=str,
        metavar="STATE|RESULT",
        default="pending,running",
        help="List jobs with specific job state or result",
    )
    parser.add_argument(
        "-n",
        "--suppress-header",
        action="store_true",
        help="Suppress printing of header line",
    )
    parser.add_argument(
        "-u",
        "--user",
        action=FilterAction,
        type=str,
        metavar="[USERNAME|UID]",
        default=str(os.getuid()),
        help="Limit output to specific username or userid "
        '(Specify "all" for all users)',
    )
    parser.add_argument(
        "-o",
        "--format",
        type=str,
        metavar="FORMAT",
        help="Specify output format using Python's string format syntax",
    )
    parser.add_argument(
        "--color",
        type=str,
        metavar="WHEN",
        choices=["never", "always", "auto"],
        default="auto",
        help="Colorize output; WHEN can be 'never', 'always', or 'auto' (default)",
    )
    parser.add_argument(
        "jobids",
        metavar="JOBID",
        type=JobID,
        nargs="*",
        help="Limit output to specific Job IDs",
    )
    # Hidden '--from-stdin' option for testing only.
    parser.add_argument("--from-stdin", action="store_true", help=argparse.SUPPRESS)
    parser.set_defaults(filtered=False)
    return parser.parse_args()


def color_setup(args, job):
    if args.color == "always" or (args.color == "auto" and sys.stdout.isatty()):
        if job.result:
            if job.result == "COMPLETED":
                sys.stdout.write("\033[01;32m")
            elif job.result == "FAILED":
                sys.stdout.write("\033[01;31m")
            elif job.result == "CANCELLED":
                sys.stdout.write("\033[37m")
            elif job.result == "TIMEOUT":
                sys.stdout.write("\033[01;31m")
            return True
    return False


def color_reset(color_set):
    if color_set:
        sys.stdout.write("\033[0;0m")


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()

    if args.jobids and args.filtered:
        LOGGER.warning("Filtering options ignored with jobid list")

    if args.format:
        fmt = args.format
    else:
        fmt = (
            "{id.f58:>12} {username:<8.8} {name:<10.10} {status_abbrev:>2.2} "
            "{ntasks:>6} {nnodes:>6h} {runtime!F:>8h} "
            "{nodelist:h}"
        )
    try:
        formatter = JobInfoFormat(fmt)
    except ValueError as err:
        raise ValueError("Error in user format: " + str(err))

    jobs = fetch_jobs(args, formatter.fields)

    if not args.suppress_header:
        print(formatter.header())

    for job in jobs:
        color_set = color_setup(args, job)
        print(formatter.format(job))
        color_reset(color_set)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
