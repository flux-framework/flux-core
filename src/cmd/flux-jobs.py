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

import os
import sys
import logging
import argparse
import pwd
import string
import errno
import fileinput
import json
from datetime import datetime, timedelta

import flux.constants
import flux.util
from flux.job import JobInfo
from flux.job import JobID

LOGGER = logging.getLogger("flux-jobs")

STATE_CONST_DICT = {
    "depend": flux.constants.FLUX_JOB_DEPEND,
    "sched": flux.constants.FLUX_JOB_SCHED,
    "run": flux.constants.FLUX_JOB_RUN,
    "cleanup": flux.constants.FLUX_JOB_CLEANUP,
    "inactive": flux.constants.FLUX_JOB_INACTIVE,
    "pending": flux.constants.FLUX_JOB_PENDING,
    "running": flux.constants.FLUX_JOB_RUNNING,
    "active": flux.constants.FLUX_JOB_ACTIVE,
}

RESULT_CONST_DICT = {
    "completed": flux.constants.FLUX_JOB_RESULT_COMPLETED,
    "failed": flux.constants.FLUX_JOB_RESULT_FAILED,
    "cancelled": flux.constants.FLUX_JOB_RESULT_CANCELLED,
    "timeout": flux.constants.FLUX_JOB_RESULT_TIMEOUT,
}


def fsd(secs):
    #  Round <1ms down to 0s for now
    if secs < 1.0e-3:
        strtmp = "0s"
    elif secs < 10.0:
        strtmp = "%.03fs" % secs
    elif secs < 60.0:
        strtmp = "%.4gs" % secs
    elif secs < (60.0 * 60.0):
        strtmp = "%.4gm" % (secs / 60.0)
    elif secs < (60.0 * 60.0 * 24.0):
        strtmp = "%.4gh" % (secs / (60.0 * 60.0))
    else:
        strtmp = "%.4gd" % (secs / (60.0 * 60.0 * 24.0))
    return strtmp


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
            job = json.loads(line)
        except ValueError as err:
            LOGGER.error("JSON input error: line %d: %s", fileinput.lineno(), str(err))
            sys.exit(1)
        jobs.append(job)
    return jobs


def list_id_cb(future, arg):
    (cbargs, jobid) = arg
    try:
        job = future.get_job()
        cbargs["jobs"].append(job)
    except EnvironmentError as err:
        if err.errno == errno.ENOENT:
            print("JobID {} unknown".format(jobid), file=sys.stderr)
        else:
            print("{}: {}".format("rpc", err.strerror), file=sys.stderr)

    cbargs["count"] += 1
    if cbargs["count"] == cbargs["total"]:
        cbargs["flux_handle"].reactor_stop()


def fetch_jobs_ids(flux_handle, args, attrs):
    cbargs = {
        "flux_handle": flux_handle,
        "jobs": [],
        "count": 0,
        "total": len(args.jobids),
    }
    for jobid in args.jobids:
        rpc_handle = flux.job.job_list_id(flux_handle, jobid, list(attrs))
        rpc_handle.then(list_id_cb, arg=(cbargs, jobid))
    ret = flux_handle.reactor_run()
    if ret < 0:
        sys.exit(1)
    return cbargs["jobs"]


def fetch_jobs_all(flux_handle, args, attrs, userid, states, results):
    rpc_handle = flux.job.job_list(
        flux_handle, args.count, list(attrs), userid, states, results
    )
    try:
        jobs = rpc_handle.get_jobs()
    except EnvironmentError as err:
        print("{}: {}".format("rpc", err.strerror), file=sys.stderr)
        sys.exit(1)
    return jobs


def calc_filters(args):
    states = 0
    results = 0
    for fname in args.filter.split(","):
        if fname.lower() in STATE_CONST_DICT:
            states |= STATE_CONST_DICT[fname.lower()]
        elif fname.lower() in RESULT_CONST_DICT:
            # Must specify "inactive" to get results
            states |= STATE_CONST_DICT["inactive"]
            results |= RESULT_CONST_DICT[fname.lower()]
        else:
            print("Invalid filter specified: {}".format(fname), file=sys.stderr)
            sys.exit(1)

    if states == 0:
        states |= flux.constants.FLUX_JOB_PENDING
        states |= flux.constants.FLUX_JOB_RUNNING

    return (states, results)


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
        "priority": ("priority",),
        "state": ("state",),
        "state_single": ("state",),
        "name": ("name",),
        "ntasks": ("ntasks",),
        "nnodes": ("nnodes",),
        "ranks": ("ranks",),
        "success": ("success",),
        "exception.occurred": ("exception_occurred",),
        "exception.severity": ("exception_severity",),
        "exception.type": ("exception_type",),
        "exception.note": ("exception_note",),
        "result": ("result",),
        "result_abbrev": ("result",),
        "t_submit": ("t_submit",),
        "t_depend": ("t_depend",),
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

    if args.jobids:
        jobs = fetch_jobs_ids(flux_handle, args, attrs)
        return jobs

    if args.a:
        args.user = str(os.getuid())
    if args.A:
        args.user = str(flux.constants.FLUX_USERID_UNKNOWN)
    if args.a or args.A:
        args.filter = "pending,running,inactive"

    if args.user == "all":
        userid = flux.constants.FLUX_USERID_UNKNOWN
    else:
        try:
            userid = pwd.getpwnam(args.user).pw_uid
        except KeyError:
            try:
                userid = int(args.user)
            except ValueError:
                print("invalid user specified", file=sys.stderr)
                sys.exit(1)

    (states, results) = calc_filters(args)

    jobs = fetch_jobs_all(flux_handle, args, attrs, userid, states, results)
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
    return [JobInfo(job) for job in lst]


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


class JobsOutputFormat(flux.util.OutputFormat):
    """
    Store a parsed version of the program's output format,
    allowing the fields to iterated without modifiers, building
    a new format suitable for headers display, etc...
    """

    class JobFormatter(string.Formatter):
        def convert_field(self, value, conv):
            """
            Flux job-specific field conversions. Avoids the need
            to create many different format field names to represent
            different conversion types. (mainly used for time-specific
            fields for now).
            """
            if conv == "d":
                # convert from float seconds sinc epoch to a datetime.
                # User can than use datetime specific format fields, e.g.
                # {t_inactive!D:%H:%M:S}.
                value = datetime.fromtimestamp(value)
            elif conv == "D":
                # As above, but convert to ISO 8601 date time string.
                value = datetime.fromtimestamp(value).strftime("%FT%T")
            elif conv == "F":
                # convert to Flux Standard Duration (fsd) string.
                value = fsd(value)
            elif conv == "H":
                # if > 0, always round up to at least one second to
                #  avoid presenting a nonzero timedelta as zero
                if 0 < value < 1:
                    value = 1
                value = str(timedelta(seconds=round(value)))
            else:
                value = super().convert_field(value, conv)
            return value

        def format_field(self, value, spec):
            if spec.endswith("h"):
                basecases = ("", "0s", "0.0", "0:00:00", "1970-01-01T00:00:00")
                value = "-" if str(value) in basecases else str(value)
                spec = spec[:-1] + "s"
            return super().format_field(value, spec)

    class HeaderFormatter(JobFormatter):
        """Custom formatter for flux-jobs(1) header row.

        Override default formatter behavior of calling getattr() on dotted
        field names. Instead look up header literally in kwargs.
        This greatly simplifies header name registration as well as
        registration of "valid" fields.
        """

        def get_field(self, field_name, args, kwargs):
            """Override get_field() so we don't do the normal gettatr thing"""
            if field_name in kwargs:
                return kwargs[field_name], None
            return super().get_field(field_name, args, kwargs)

    #  List of legal format fields and their header names
    headings = {
        "id": "JOBID",
        "id.dec": "JOBID",
        "id.hex": "JOBID",
        "id.f58": "JOBID",
        "id.kvs": "JOBID",
        "id.words": "JOBID",
        "id.dothex": "JOBID",
        "userid": "UID",
        "username": "USER",
        "priority": "PRI",
        "state": "STATE",
        "state_single": "ST",
        "name": "NAME",
        "ntasks": "NTASKS",
        "nnodes": "NNODES",
        "expiration": "EXPIRATION",
        "t_remaining": "T_REMAINING",
        "ranks": "RANKS",
        "success": "SUCCESS",
        "result": "RESULT",
        "result_abbrev": "RS",
        "t_submit": "T_SUBMIT",
        "t_depend": "T_DEPEND",
        "t_sched": "T_SCHED",
        "t_run": "T_RUN",
        "t_cleanup": "T_CLEANUP",
        "t_inactive": "T_INACTIVE",
        "runtime": "RUNTIME",
        "status": "STATUS",
        "status_abbrev": "ST",
        "exception.occurred": "EXCEPTION-OCCURRED",
        "exception.severity": "EXCEPTION-SEVERITY",
        "exception.type": "EXCEPTION-TYPE",
        "exception.note": "EXCEPTION-NOTE",
        "annotations": "ANNOTATIONS",
        # The following are special pre-defined cases per RFC27
        "annotations.sched.t_estimate": "T_ESTIMATE",
        "annotations.sched.reason_pending": "REASON",
        "annotations.sched.resource_summary": "RESOURCES",
        "sched": "SCHED",
        "sched.t_estimate": "T_ESTIMATE",
        "sched.reason_pending": "REASON",
        "sched.resource_summary": "RESOURCES",
        "user": "USER",
    }

    def __init__(self, fmt):
        """
        Parse the input format fmt with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Throws an exception if any format fields do not match the allowed
        list of headings above.

        Special case for annotations, which may be arbitrary
        creations of scheduler or user.
        """
        format_list = string.Formatter().parse(fmt)
        for (_, field, _, _) in format_list:
            if field and not field in self.headings:
                if field.startswith("annotations."):
                    field_heading = field[len("annotations.") :].upper()
                    self.headings[field] = field_heading
                elif field.startswith("sched.") or field.startswith("user."):
                    field_heading = field.upper()
                    self.headings[field] = field_heading
        super().__init__(self.headings, fmt, prepend="0.")

    def format(self, obj):
        """
        format object with our JobFormatter
        """
        return self.JobFormatter().format(self.get_format(), obj)

    def header(self):
        """
        format header with custom HeaderFormatter
        """
        return self.HeaderFormatter().format(self.header_format(), **self.headings)


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
            "{ranks:h}"
        )
    try:
        formatter = JobsOutputFormat(fmt)
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
