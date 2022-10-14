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
from flux.job import JobID, JobInfo, JobInfoFormat, JobList, job_fields_to_attrs
from flux.job.stats import JobStats
from flux.util import UtilConfig

LOGGER = logging.getLogger("flux-jobs")


class FluxJobsConfig(UtilConfig):
    """flux-jobs specific user configuration class"""

    builtin_formats = {
        "default": {
            "description": "Default flux-jobs format string",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} {username:<8.8} {name:<10.10+} "
                "{status_abbrev:>2.2} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "cute": {
            "description": "Cute flux-jobs format string (default with emojis)",
            "format": (
                "{id.f58:>12} ?:{queue:<8.8} {username:<8.8} {name:<10.10+} "
                "{status_emoji:>5.5} {ntasks:>6} {nnodes:>6h} "
                "{contextual_time!F:>8h} {contextual_info}"
            ),
        },
        "long": {
            "description": "Extended flux-jobs format string",
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

    def validate_formats(self, path, formats):
        if not isinstance(formats, dict):
            raise ValueError(f"{path}: the 'formats' key must be a mapping")
        for name, entry in formats.items():
            if name in self.builtin_formats:
                raise ValueError(
                    f"{path}: override of builtin format '{name}' not permitted"
                )
            if not isinstance(entry, dict):
                raise ValueError(f"{path}: 'formats.{name}' must be a mapping")
            if "format" not in entry:
                raise ValueError(
                    f"{path}: formats.{name}: required key 'format' missing"
                )
            if not isinstance(entry["format"], str):
                raise ValueError(
                    f"{path}: formats.{name}: 'format' key must be a string"
                )

    def validate(self, path, config):
        """Validate a loaded flux-jobs config file as dictionary"""
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")


def load_format_string(args):
    if "{" in args.format:
        return args.format

    config = FluxJobsConfig().load()

    if args.format == "help":
        print("\nConfigured flux-jobs output formats:\n")
        for name, entry in config.formats.items():
            print(f"  {name:<12}", end="")
            try:
                print(f" {entry['description']}")
            except KeyError:
                print()
        sys.exit(0)
    elif args.format.startswith("get-config="):
        _, name = args.format.split("=", 1)
        try:
            entry = config.formats[name]
        except KeyError:
            LOGGER.error("--format: No such format %s", name)
            sys.exit(1)
        print(f"[formats.{name}]")
        try:
            print(f"description = \"{entry['description']}\"")
        except KeyError:
            pass
        print(f"format = \"{entry['format']}\"")
        sys.exit(0)
    try:
        return config.formats[args.format]["format"]
    except KeyError:
        LOGGER.error("--format: No such format %s", args.format)
        sys.exit(1)


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
        args.filter.update(["pending", "running", "inactive"])

    if not args.filter:
        args.filter = {"pending", "running"}

    jobs_rpc = JobList(
        flux_handle,
        ids=args.jobids,
        attrs=attrs,
        filters=args.filter,
        user=args.user,
        max_entries=args.count,
        since=since,
        name=args.name,
        queue=args.queue,
    )

    jobs = jobs_rpc.jobs()

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


class FilterActionSetUpdate(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, "filtered", True)
        values = values.split(",")
        getattr(namespace, self.dest).update(values)


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
        help="Limit output to N jobs(default 1000)",
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
        "--name",
        action=FilterAction,
        type=str,
        metavar="JOB-NAME",
        help="Limit output to specific job name",
    )
    parser.add_argument(
        "--queue",
        action=FilterAction,
        type=str,
        metavar="QUEUE",
        help="Limit output to specific queue",
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
        "--color",
        type=str,
        metavar="WHEN",
        choices=["never", "always", "auto"],
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
        jobs = fetch_jobs_flux(args, fields, flux_handle=handle)
        stats = None
        if args.stats:
            stats = JobStats(handle).update_sync()
    except (OSError, FileNotFoundError):
        pass
    return (job, jobs, stats)


def print_jobs(jobs, args, formatter, path="", level=0):
    children = []

    def pre(job):
        job.color_set = color_setup(args, job)

    def post(job):
        color_reset(job.color_set)
        if args.recursive and is_user_instance(job, args):
            children.append(job)

    formatter.print_items(jobs, no_header=True, pre=pre, post=post)

    if not args.recursive or args.level == level:
        return

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
        thispath = f"{path}{job.id.f58}"
        print(f"\n{thispath}:")
        if stats:
            print(
                f"{stats.running} running, {stats.successful} completed, "
                f"{stats.failed} failed, {stats.pending} pending"
            )

        print_jobs(jobs, args, formatter, path=thispath, level=level + 1)


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()

    if args.jobids and args.filtered and not args.recursive:
        LOGGER.warning("Filtering options ignored with jobid list")

    if args.recurse_all:
        args.recursive = True

    fmt = load_format_string(args)
    try:
        formatter = JobInfoFormat(fmt)
    except ValueError as err:
        raise ValueError("Error in user format: " + str(err))

    if args.stats or args.stats_only:
        stats = JobStats(flux.Flux()).update_sync()
        print(
            f"{stats.running} running, {stats.successful} completed, "
            f"{stats.failed} failed, {stats.pending} pending"
        )
        if args.stats_only:
            sys.exit(0 if stats.active else 1)

    jobs = fetch_jobs(args, formatter.fields)
    sformatter = JobInfoFormat(formatter.filter_empty(jobs))

    if not args.suppress_header:
        print(sformatter.header())

    print_jobs(jobs, args, sformatter)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
