##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import concurrent.futures
import fileinput
import json
import logging
import os
import sys

import flux.constants
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.job import JobID, JobInfo, JobInfoFormat, JobList, job_fields_to_attrs
from flux.job.stats import JobStats
from flux.util import (
    FilterAction,
    FilterActionSetUpdate,
    FilterActionUser,
    FilterTrueAction,
    UtilConfig,
    help_formatter,
)

LOGGER = logging.getLogger("flux-jobs")


class FluxJobsConfig(UtilConfig):
    """flux-jobs specific user configuration class"""

    builtin_formats = {
        "default": {
            "description": "Default flux-jobs format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8.8} {name:<10.10+} "
                "{status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "cute": {
            "description": "Cute flux-jobs format string (default with emojis)",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8.8} {name:<10.10+} "
                "{status_emoji:>5.5} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "long": {
            "description": "Extended flux-jobs format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8.8} {name:<10.10+} "
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
        "endreason": {
            "description": "Show why each job ended",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} +:{username:<8.8} {name:<10.10+} "
                "{status_abbrev:>2.2} {t_inactive!d:%b%d %R::>12h} {inactive_reason}"
            ),
        },
    }

    def __init__(self):
        initial_dict = {"formats": dict(self.builtin_formats)}
        super().__init__(name="flux-jobs", initial_dict=initial_dict)

    def validate(self, path, config):
        """Validate a loaded flux-jobs config file as dictionary"""
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")


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
    return (jobs, False)


def need_instance_info(fields):
    for field in fields:
        if field.startswith("instance."):
            return True
    return False


# pylint: disable=too-many-branches
def fetch_jobs_flux(args, fields, flux_handle=None):
    if not flux_handle:
        flux_handle = flux.Flux()

    attrs = job_fields_to_attrs(fields)

    if args.color == "always" or args.color == "auto":
        attrs.update(job_fields_to_attrs(["result", "annotations"]))
    if args.recursive:
        attrs.update(job_fields_to_attrs(["annotations", "status", "userid"]))
    if args.json:
        args.no_header = True
        attrs.add("all")

    if args.A:
        args.user = str(flux.constants.FLUX_USERID_UNKNOWN)

    since = 0.0
    if args.since:
        # Implies -a, *unless* another filter option is already in effect:
        if not args.filter:
            args.a = True

        try:
            since = flux.util.parse_datetime(args.since).timestamp()
        except ValueError:
            since = float(args.since)

        # Ensure args.since is in the past
        if since > flux.util.parse_datetime("now").timestamp():
            LOGGER.error("--since=%s appears to be in the future", args.since)
            sys.exit(1)

    if args.a:
        if args.filter:
            LOGGER.warning("Both -a and --filter specified, ignoring -a")
        else:
            args.filter.update(["pending", "running", "inactive"])

    if not args.filter:
        args.filter = {"pending", "running"}

    constraint = None
    if args.include:
        try:
            constraint = {"ranks": [IDset(args.include).encode()]}
        except ValueError:
            try:
                constraint = {"hostlist": [Hostlist(args.include).encode()]}
            except ValueError:
                raise ValueError(f"-i/--include: invalid targets: {args.include}")
        # --include implies check against all user jobs
        if not args.filtereduser:
            args.user = str(flux.constants.FLUX_USERID_UNKNOWN)

    # max_entries set to args.count + 1 to detect truncation for a user warning
    jobs_rpc = JobList(
        flux_handle,
        ids=args.jobids,
        attrs=attrs,
        filters=args.filter,
        user=args.user,
        max_entries=(args.count + 1 if args.count > 0 else args.count),
        since=since,
        name=args.name,
        queue=args.queue,
        constraint=constraint,
    )

    jobs = jobs_rpc.jobs()

    if args.count > 0 and len(jobs) > args.count:
        jobs = jobs[0:-1]
        truncated = True
    else:
        truncated = False

    if need_instance_info(fields):
        with concurrent.futures.ThreadPoolExecutor(args.threads) as executor:
            concurrent.futures.wait(
                [executor.submit(job.get_instance_info) for job in jobs]
            )

    #  Print all errors accumulated in JobList RPC:
    try:
        for err in jobs_rpc.errors:
            print(err, file=sys.stderr)
    except EnvironmentError:
        pass

    return (jobs, truncated)


def fetch_jobs(args, fields):
    """
    Fetch jobs from flux or optionally stdin.

    Returns tuple containing a list of JobInfo objects, and flag
    indicating if result truncated
    """
    if args.from_stdin:
        rv = fetch_jobs_stdin()
    else:
        rv = fetch_jobs_flux(args, fields)
    return rv


def parse_args():
    parser = argparse.ArgumentParser(prog="flux-jobs", formatter_class=help_formatter())
    # -a equivalent to -s "pending,running,inactive" and -u set to userid
    parser.add_argument("-a", action=FilterTrueAction, help="List jobs in all states")
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
        help="Limit output to N jobs (default 1000)",
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
        "--since",
        action=FilterAction,
        type=str,
        metavar="WHEN",
        default=0.0,
        help="Include jobs that have become inactive since WHEN. "
        + "(implies -a if no other --filter option is specified)",
    )
    parser.add_argument(
        "-n",
        "--no-header",
        action="store_true",
        help="Suppress printing of header line",
    )
    # --suppress-header, older legacy name
    parser.add_argument(
        "--suppress-header",
        action="store_true",
        dest="no_header",
        help=argparse.SUPPRESS,
    )
    parser.add_argument(
        "-u",
        "--user",
        action=FilterActionUser,
        type=str,
        metavar="[USERNAME|UID]",
        default=str(os.getuid()),
        help="Limit output to specific username or userid "
        + '(Specify "all" for all users)',
    )
    parser.add_argument(
        "--name",
        action=FilterAction,
        type=str,
        metavar="JOB-NAME",
        help="Limit output to specific job name",
    )
    parser.add_argument(
        "-q",
        "--queue",
        action=FilterActionSetUpdate,
        default=set(),
        metavar="QUEUE,...",
        help="Limit output to specific queue or queues",
    )
    parser.add_argument(
        "-i",
        "--include",
        type=str,
        metavar="HOSTS|RANKS",
        help="Limit output to jobs that were allocated to the specified "
        + "HOSTS or RANKS provided as a hostlist or idset.",
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
    parser.add_argument(
        "--sort",
        type=str,
        default="",
        metavar="KEY,...",
        help="Specify sort order",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output jobs in JSON instead of formatted output",
    )
    parser.add_argument(
        "--color",
        type=str,
        metavar="WHEN",
        choices=["never", "always", "auto"],
        nargs="?",
        const="always",
        default="auto",
        help="Colorize output; WHEN can be 'never', 'always', or 'auto' (default)",
    )
    parser.add_argument(
        "-R",
        "--recursive",
        action="store_true",
        help="List jobs recursively",
    )
    parser.add_argument(
        "-L",
        "--level",
        type=int,
        metavar="N",
        default=9999,
        help="With --recursive, only descend N levels",
    )
    parser.add_argument(
        "--recurse-all",
        action="store_true",
        help="With --recursive, attempt to recurse all jobs, "
        + "not just jobs of current user",
    )
    parser.add_argument(
        "--threads",
        type=int,
        metavar="N",
        help="Set max number of worker threads",
    )
    parser.add_argument(
        "--stats", action="store_true", help="Print job statistics before header"
    )
    parser.add_argument(
        "--stats-only",
        action="store_true",
        help="Print job statistics only and exit. Exits with non-zero status "
        "if there are no active jobs. Allows usage like: "
        "'while flux jobs --stats-only; do sleep 1; done'",
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
    parser.set_defaults(filtereduser=False)
    return parser.parse_args()


def color_setup(args, job):
    if args.color == "always" or (args.color == "auto" and sys.stdout.isatty()):
        if job.result:
            if job.result == "COMPLETED":
                sys.stdout.write("\033[01;32m")
            elif job.result == "FAILED":
                sys.stdout.write("\033[01;31m")
            elif job.result == "CANCELED":
                sys.stdout.write("\033[37m")
            elif job.result == "TIMEOUT":
                sys.stdout.write("\033[01;31m")
            return True
        if job.uri:
            sys.stdout.write("\033[01;34m")
            return True
    return False


def color_reset(color_set):
    if color_set:
        sys.stdout.write("\033[0;0m")


def is_user_instance(job, args):
    """Return True if this job should be target of recursive job list"""
    return (
        job.uri
        and job.status_abbrev == "R"
        and (args.recurse_all or job.userid == os.getuid())
    )


def get_jobs_recursive(job, args, fields):
    jobs = []
    stats = None
    try:
        #  Don't generate an error if we fail to connect to this
        #   job. This could be because job services aren't up yet,
        #   (OSError with errno ENOSYS) or this user is not the owner
        #   of the job. Either way, simply skip descending into the job
        #
        handle = flux.Flux(str(job.uri))
        (jobs, truncated) = fetch_jobs_flux(args, fields, flux_handle=handle)
        stats = None
        if args.stats:
            stats = JobStats(handle).update_sync()
    except (OSError, FileNotFoundError):
        pass
    return (job, jobs, stats)


def print_jobs(jobs, args, formatter, path="", level=0):
    children = []
    # Array of jobs as dict
    result = []
    # Convenience dict for looking up job in result by jobid:
    job_dict = {}

    def pre(job):
        job.color_set = color_setup(args, job)

    def post(job):
        color_reset(job.color_set)
        if args.recursive and is_user_instance(job, args):
            children.append(job)

    if args.json:
        for job in jobs:
            jdict = job.to_dict()
            result.append(jdict)
            job_dict[job.id] = jdict
            #  Normally, print_items() handles collection of any children
            #  in this job as a side effect of printing each line of output.
            #  So, when generating JSON output instead, do that explicitly
            #  here:
            if args.recursive and is_user_instance(job, args):
                children.append(job)
    else:
        formatter.print_items(jobs, no_header=True, pre=pre, post=post)

    if not args.recursive or args.level == level:
        return result

    #  Reset args.jobids since it won't apply recursively:
    args.jobids = None

    futures = []
    with concurrent.futures.ThreadPoolExecutor(args.threads) as executor:
        for job in children:
            futures.append(
                executor.submit(get_jobs_recursive, job, args, formatter.fields)
            )

    if path:
        path = f"{path}/"

    for future in futures:
        (job, jobs, stats) = future.result()

        #  If generating JSON, just add this job's children to a job["jobs"]
        #  array and continue:
        if args.json:
            job_dict[job.id]["jobs"] = [x.to_dict() for x in jobs]
            continue

        #  Otherwise generate a header for this jobs children and print them:
        thispath = f"{path}{job.id.f58}"
        print(f"\n{thispath}:")
        if stats:
            print(
                f"{stats.running} running, {stats.successful} completed, "
                f"{stats.failed} failed, {stats.pending} pending"
            )
        print_jobs(jobs, args, formatter, path=thispath, level=level + 1)

    return result


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()

    if args.json and (args.stats or args.stats_only):
        LOGGER.error("--json incompatible with --stats or --stats-only")
        sys.exit(1)

    if args.jobids and args.filtered and not args.recursive:
        LOGGER.warning("Filtering options ignored with jobid list")

    if args.recurse_all:
        args.recursive = True

    fmt = FluxJobsConfig().load().get_format_string(args.format)
    try:
        formatter = JobInfoFormat(fmt)
    except ValueError as err:
        raise ValueError("Error in user format: " + str(err))

    if args.stats or args.stats_only:
        try:
            stats = JobStats(flux.Flux(), queue=args.queue).update_sync()
        except ValueError as err:
            LOGGER.error("error retrieving job stats: %s", str(err))
            sys.exit(1)

        print(
            f"{stats.running} running, {stats.successful} completed, "
            f"{stats.failed} failed, {stats.pending} pending, "
            f"{stats.inactive_purged} inactive purged"
        )
        if args.stats_only:
            sys.exit(0 if stats.active else 1)

    if args.sort:
        formatter.set_sort_keys(args.sort)

    (jobs, truncated) = fetch_jobs(args, formatter.fields)
    sformatter = JobInfoFormat(formatter.filter(jobs))

    if not args.no_header:
        print(sformatter.header())

    result = print_jobs(jobs, args, sformatter)
    if args.json:
        # Only emit single JSON object if user asked for one specific job
        if args.jobids and len(args.jobids) == 1:
            if result:
                print(json.dumps(result[0]))
        else:
            print(json.dumps({"jobs": result}))

    # "no_header" also applies to trailers :-)
    if not args.no_header and truncated:
        print(f"warning: output truncated at {args.count} jobs.", file=sys.stderr)
        print(
            "Use --count to increase returned results or use filters to alter results.",
            file=sys.stderr,
        )


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
