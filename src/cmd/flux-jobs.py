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
import time
import pwd
import string
import errno
from datetime import timedelta

import flux.job
import flux.constants
import flux.util
from flux.core.inner import raw
from flux.memoized_property import memoized_property

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
}


def fsd(secs, hyphenifzero):
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
    if hyphenifzero and strtmp == "0s":
        return "-"
    return strtmp


def statetostr(stateid, singlechar=False):
    return raw.flux_job_statetostr(stateid, singlechar).decode("utf-8")


def resulttostr(resultid, singlechar=False):
    # if result not returned, just return empty string back
    if resultid == "":
        return ""
    return raw.flux_job_resulttostr(resultid, singlechar).decode("utf-8")


def statustostr(stateid, resultid, abbrev=False):
    if stateid & flux.constants.FLUX_JOB_PENDING:
        statusstr = "PD" if abbrev else "PENDING"
    elif stateid & flux.constants.FLUX_JOB_RUNNING:
        statusstr = "R" if abbrev else "RUNNING"
    else:  # flux.constants.FLUX_JOB_INACTIVE
        statusstr = resulttostr(resultid, abbrev)
    return statusstr


def get_username(userid):
    try:
        return pwd.getpwuid(userid).pw_name
    except KeyError:
        return str(userid)


class ExceptionInfo:
    def __init__(self, occurred, severity, _type, note):
        self.occurred = occurred
        self.severity = severity
        self.type = _type
        self.note = note


class JobInfo:
    """
    JobInfo class: encapsulate job-info.list response in an object
    that implements a getattr interface to job information with
    memoization. Better for use with output formats since results
    are only computed as-needed.
    """

    #  Default values for job properties.
    defaults = {
        "t_depend": 0.0,
        "t_sched": 0.0,
        "t_run": 0.0,
        "t_cleanup": 0.0,
        "t_inactive": 0.0,
        "nnodes": "",
        "ranks": "",
        "success": "",
        "result": "",
    }

    def __init__(self, info_resp):
        #  Set defaults, then update with job-info.list response items:
        combined_dict = self.defaults.copy()
        combined_dict.update(info_resp)

        #  Rename "state" to "state_id" and "result" to "result_id"
        #  until returned state is a string:
        if "state" in combined_dict:
            combined_dict["state_id"] = combined_dict.pop("state")

        if "result" in combined_dict:
            combined_dict["result_id"] = combined_dict.pop("result")

        # Overwrite "exception" with our exception object
        exc1 = combined_dict.get("exception_occurred", "")
        exc2 = combined_dict.get("exception_severity", "")
        exc3 = combined_dict.get("exception_type", "")
        exc4 = combined_dict.get("exception_note", "")
        combined_dict["exception"] = ExceptionInfo(exc1, exc2, exc3, exc4)

        #  Set all keys as self._{key} to be found by getattr and
        #   memoized_property decorator:
        for key, value in combined_dict.items():
            setattr(self, "_{0}".format(key), value)

    #  getattr method to return all non-computed values in job-info.list
    #   response by default. Avoids the need to wrap @property methods
    #   that just return self._<attr>.
    #
    def __getattr__(self, attr):
        if attr.startswith("_"):
            raise AttributeError
        try:
            return getattr(self, "_{0}".format(attr))
        except KeyError:
            raise AttributeError("invalid JobInfo attribute '{}'".format(attr))

    def get_runtime(self, roundup=False):
        if self.t_cleanup > 0 and self.t_run > 0:
            runtime = self.t_cleanup - self.t_run
            if roundup:
                runtime = round(runtime + 0.5)
        elif self.t_run > 0:
            runtime = time.time() - self.t_run
            if roundup:
                runtime = round(runtime + 0.5)
        else:
            runtime = 0.0
        return runtime

    @memoized_property
    def state(self):
        return statetostr(self.state_id)

    @memoized_property
    def state_single(self):
        return statetostr(self.state_id, True)

    @memoized_property
    def result(self):
        return resulttostr(self.result_id)

    @memoized_property
    def result_abbrev(self):
        return resulttostr(self.result_id, True)

    @memoized_property
    def username(self):
        return get_username(self.userid)

    @memoized_property
    def runtime_fsd(self):
        return fsd(self.runtime, False)

    @memoized_property
    def runtime_hms(self):
        runtime = self.get_runtime(True)
        return str(timedelta(seconds=runtime))

    @memoized_property
    def runtime(self):
        return self.get_runtime()

    @memoized_property
    def status(self):
        return statustostr(self.state_id, self.result_id, False)

    @memoized_property
    def status_abbrev(self):
        return statustostr(self.state_id, self.result_id, True)


def fetch_jobs_stdin():
    """
    Return a list of jobs gathered from a series of JSON objects, one per
    line, presented on stdin. This function is used for testing of the
    flux-jobs utility, and thus, all filtering options are currently
    ignored.
    """
    import fileinput
    import json

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
        cbargs["flux_handle"].reactor_stop(future.get_reactor())


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
    ret = flux_handle.reactor_run(rpc_handle.get_reactor(), 0)
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
        "runtime_fsd": ("t_run", "t_cleanup"),
        "runtime_hms": ("t_run", "t_cleanup"),
        "status": ("state", "result"),
        "status_abbrev": ("state", "result"),
    }

    attrs = set()
    for field in fields:
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
        type=int,
        metavar="JOBID",
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
        def format_field(self, value, spec):
            if spec.endswith("h"):
                basecases = ("", "0s", "0.0", "0:00:00")
                value = "-" if str(value) in basecases else str(value)
                spec = spec[:-1] + "s"
            return super().format_field(value, spec)

    #  List of legal format fields and their header names
    #  - Note special cases added in constructor below
    headings = {
        "id": "JOBID",
        "userid": "UID",
        "username": "USER",
        "priority": "PRI",
        "state": "STATE",
        "state_single": "ST",
        "name": "NAME",
        "ntasks": "NTASKS",
        "nnodes": "NNODES",
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
        "runtime_fsd": "RUNTIME",
        "runtime_hms": "RUNTIME",
        "status": "STATUS",
        "status_abbrev": "STATUS",
    }

    exception_headings = ExceptionInfo(
        "EXCEPTION-OCCURRED", "EXCEPTION-SEVERITY", "EXCEPTION-TYPE", "EXCEPTION-NOTE"
    )

    def __init__(self, fmt):
        """
        Parse the input format fmt with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Throws an exception if any format fields do not match the allowed
        list of headings above.
        """
        # Add some special format fields just for the validity checks,
        # values are held in other objects
        self.headings["exception.occurred"] = None
        self.headings["exception.severity"] = None
        self.headings["exception.type"] = None
        self.headings["exception.note"] = None
        super().__init__(self.headings, fmt)

    def get_format(self):
        """
        Return the format string with prepended `0.` if necessary.
        """
        try:
            # pylint: disable=access-member-before-definition
            return self._jobfmt
        except AttributeError:
            pass

        lst = []
        for (text, field, spec, conv) in self.format_list:
            # If field doesn't have `0.` then add it
            if field and not field.startswith("0."):
                field = "0." + field
            lst.append(self._fmt_tuple(text, field, spec, conv))
        # pylint: disable=attribute-defined-outside-init
        self._jobfmt = "".join(lst)
        return self._jobfmt

    def format(self, obj):
        """
        format object with our JobFormatter
        """
        return self.JobFormatter().format(self.get_format(), obj)

    def header(self):
        """
        format header with our JobFormatter
        """
        return self.JobFormatter().format(
            self.header_format(), **self.headings, exception=self.exception_headings
        )


def color_setup(args, job):
    if args.color == "always" or (args.color == "auto" and sys.stdout.isatty()):
        if job.result:
            if job.result == "COMPLETED":
                sys.stdout.write("\033[01;32m")
            elif job.result == "FAILED":
                sys.stdout.write("\033[01;31m")
            elif job.result == "CANCELLED":
                sys.stdout.write("\033[37m")
            return True
    return False


def color_reset(color_set):
    if color_set:
        sys.stdout.write("\033[0;0m")


@flux.util.CLIMain(LOGGER)
def main():
    args = parse_args()

    if args.jobids and args.filtered:
        LOGGER.warning("Filtering options ignored with jobid list")

    if args.format:
        fmt = args.format
    else:
        fmt = (
            "{id:>18} {username:<8.8} {name:<10.10} {status_abbrev:>6.6} "
            "{ntasks:>6} {nnodes:>6h} {runtime_fsd:>8h} "
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
