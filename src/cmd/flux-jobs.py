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
from datetime import timedelta

import flux.job
import flux.constants
import flux.util
from flux.core.inner import raw
from flux.memoized_property import memoized_property

logger = logging.getLogger("flux-jobs")

state_const_dict = {
    "depend": flux.constants.FLUX_JOB_DEPEND,
    "sched": flux.constants.FLUX_JOB_SCHED,
    "run": flux.constants.FLUX_JOB_RUN,
    "cleanup": flux.constants.FLUX_JOB_CLEANUP,
    "inactive": flux.constants.FLUX_JOB_INACTIVE,
    "pending": flux.constants.FLUX_JOB_PENDING,
    "running": flux.constants.FLUX_JOB_RUNNING,
    "active": flux.constants.FLUX_JOB_ACTIVE,
}


def fsd(t, hyphenifzero):
    #  Round <1ms down to 0s for now
    if t < 1.0e-3:
        s = "0s"
    elif t < 10.0:
        s = "%.03fs" % t
    elif t < 60.0:
        s = "%.4gs" % t
    elif t < (60.0 * 60.0):
        s = "%.4gm" % (t / 60.0)
    elif t < (60.0 * 60.0 * 24.0):
        s = "%.4gh" % (t / (60.0 * 60.0))
    else:
        s = "%.4gd" % (t / (60.0 * 60.0 * 24.0))
    if hyphenifzero and s == "0s":
        return "-"
    return s


def statetostr(stateid, singlechar=False):
    return raw.flux_job_statetostr(stateid, singlechar).decode("utf-8")


def get_username(userid):
    try:
        return pwd.getpwuid(userid).pw_name
    except KeyError:
        return str(userid)


class JobInfo:
    """
    JobInfo class: encapsulate job-info.list response in an object
    that implements a getattr interface to job information with
    memoization. Better for use with output formats since results
    are only computed as-needed.
    """

    #  Default values for job properties.
    defaults = dict(
        t_depend=0.0,
        t_sched=0.0,
        t_run=0.0,
        t_cleanup=0.0,
        t_inactive=0.0,
        nnodes="",
        ranks="",
    )

    def __init__(self, info_resp):
        #  Set defaults, then update with job-info.list response items:
        d = self.defaults.copy()
        d.update(info_resp)

        #  Rename "state" to "state_id" until returned state is a string:
        d["state_id"] = d.pop("state")

        #  Set all keys as self._{key} to be found by getattr and
        #   memoized_property decorator:
        for key, value in d.items():
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
            t = self.t_cleanup - self.t_run
            if roundup:
                t = round(t + 0.5)
        elif self.t_run > 0:
            t = time.time() - self.t_run
            if roundup:
                t = round(t + 0.5)
        else:
            t = 0.0
        return t

    @memoized_property
    def state(self):
        return statetostr(self.state_id)

    @memoized_property
    def state_single(self):
        return statetostr(self.state_id, True)

    @memoized_property
    def username(self):
        return get_username(self.userid)

    @memoized_property
    def nnodes_hyphen(self):
        return self.nnodes or "-"

    @memoized_property
    def ranks_hyphen(self):
        return self.ranks or "-"

    @memoized_property
    def runtime_fsd(self):
        return fsd(self.runtime, False)

    @memoized_property
    def runtime_fsd_hyphen(self):
        return fsd(self.runtime, True)

    @memoized_property
    def runtime_hms(self):
        t = self.get_runtime(True)
        return str(timedelta(seconds=t))

    @memoized_property
    def runtime(self):
        return self.get_runtime()


def fetch_jobs_stdin(args):
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
            logger.error(
                "JSON input error: line {}: {}".format(fileinput.lineno(), err)
            )
            sys.exit(1)
        jobs.append(job)
    return jobs


def fetch_jobs_flux(args):
    h = flux.Flux()

    # Future optimization, reduce attrs based on what is in output
    # format, to reduce potentially large return RPC.
    attrs = [
        "userid",
        "priority",
        "state",
        "name",
        "ntasks",
        "nnodes",
        "ranks",
        "t_submit",
        "t_depend",
        "t_sched",
        "t_run",
        "t_cleanup",
        "t_inactive",
    ]

    if args.a:
        args.user = str(os.geteuid())
    if args.A:
        args.user = str(flux.constants.FLUX_USERID_UNKNOWN)
    if args.a or args.A:
        args.states = "pending,running,inactive"

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

    states = 0
    for state in args.states.split(","):
        try:
            states |= state_const_dict[state.lower()]
        except KeyError:
            print("Invalid state specified: {}".format(state), file=sys.stderr)
            sys.exit(1)

    if states == 0:
        states |= flux.constants.FLUX_JOB_PENDING
        states |= flux.constants.FLUX_JOB_RUNNING

    rpc_handle = flux.job.list(h, args.count, attrs, userid, states)
    try:
        jobs = rpc_handle.get_jobs()
    except EnvironmentError as e:
        print("{}: {}".format("rpc", e.strerror), file=sys.stderr)
        sys.exit(1)

    return jobs


def fetch_jobs(args):
    """
    Fetch jobs from flux or optionally stdin.
    Returns a list of JobInfo objects
    """
    if args.from_stdin:
        l = fetch_jobs_stdin(args)
    else:
        l = fetch_jobs_flux(args)
    return [JobInfo(job) for job in l]


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-jobs", formatter_class=flux.util.help_formatter()
    )
    # -a equivalent to -s "pending,running,inactive" and -u set to userid
    parser.add_argument(
        "-a", action="store_true", help="List all jobs for current user"
    )
    # -A equivalent to -s "pending,running,inactive" and -u set to "all"
    parser.add_argument("-A", action="store_true", help="List all jobs for all users")
    parser.add_argument(
        "-c",
        "--count",
        type=int,
        metavar="N",
        default=1000,
        help="Limit output to N jobs(default 1000)",
    )
    parser.add_argument(
        "-s",
        "--states",
        type=str,
        metavar="STATES",
        default="pending,running",
        help="List jobs in specific job states or virtual job states",
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
        type=str,
        metavar="[USERNAME|UID]",
        default=str(os.geteuid()),
        help='Limit output to specific username or userid (Specify "all" for all users)',
    )
    parser.add_argument(
        "-o",
        "--format",
        type=str,
        metavar="FORMAT",
        help="Specify output format using Python's string format syntax",
    )
    # Hidden '--from-stdin' option for testing only.
    parser.add_argument("--from-stdin", action="store_true", help=argparse.SUPPRESS)
    return parser.parse_args()


class OutputFormat:
    """
    Store a parsed version of the program's output format,
    allowing the fields to iterated without modifiers, building
    a new format suitable for headers display, etc...
    """

    #  List of legal format fields and their header names
    headings = dict(
        id="JOBID",
        userid="UID",
        username="USER",
        priority="PRI",
        state="STATE",
        state_single="STATE",
        name="NAME",
        ntasks="NTASKS",
        nnodes="NNODES",
        nnodes_hyphen="NNODES",
        ranks="RANKS",
        ranks_hyphen="RANKS",
        t_submit="T_SUBMIT",
        t_depend="T_DEPEND",
        t_sched="T_SCHED",
        t_run="T_RUN",
        t_cleanup="T_CLEANUP",
        t_inactive="T_INACTIVE",
        runtime="RUNTIME",
        runtime_fsd="RUNTIME",
        runtime_fsd_hyphen="RUNTIME",
        runtime_hms="RUNTIME",
    )

    def __init__(self, fmt):
        """
        Parse the input format fmt with string.Formatter.
        Save off the fields and list of format tokens for later use,
        (converting None to "" in the process)

        Throws an exception if any format fields do not match the allowed
        list of headings above.
        """
        from string import Formatter

        self.fmt = fmt
        #  Parse format into list of (string, field, spec, conv) tuples,
        #   replacing any None values with empty string "" (this makes
        #   substitution back into a format string in self.header() and
        #   self.get_format() much simpler below)
        l = Formatter().parse(fmt)
        self.format_list = [[s or "" for s in t] for t in l]

        #  Store list of requested fields in self.fields
        self.fields = [field for (s, field, spec, conv) in self.format_list]

        #  Throw an exception if any requested fields are invalid:
        for field in self.fields:
            if field and not field in self.headings:
                raise ValueError("Unknown format field: " + field)

    def _fmt_tuple(self, s, field, spec, conv):
        #  If field is empty string or None, then the result of the
        #   format (besides 's') doesn't make sense, just return 's'
        if not field:
            return s
        #  The prefix of spec and conv are stripped by formatter.parse()
        #   replace them here if the values are not empty:
        spec = ":" + spec if spec else ""
        conv = "!" + conv if conv else ""
        return "{0}{{{1}{2}{3}}}".format(s, field, conv, spec)

    def header(self):
        """
        Return the header row formatted by the user-provided format spec,
        which will be made "safe" for use with string headings.
        """
        import re

        l = []
        for (s, field, spec, conv) in self.format_list:
            #  Remove floating point formatting on any spec:
            spec = re.sub(r"\.\d+[bcdoxXeEfFgGn%]$", "", spec)
            l.append(self._fmt_tuple(s, field, spec, conv))
        fmt = "".join(l)
        return fmt.format(**self.headings)

    def get_format(self):
        """
        Return the format string with prepended `0.` if necessary.
        """
        try:
            return self.jobfmt
        except AttributeError:
            pass

        l = []
        for (s, field, spec, conv) in self.format_list:
            # If field doesn't have `0.` then add it
            if field and not field.startswith("0."):
                field = "0." + field
            l.append(self._fmt_tuple(s, field, spec, conv))
        self.jobfmt = "".join(l)
        return self.jobfmt

    def format(self, job):
        """
        format JobInfo object with internal format
        prepend `0.` if necessary to fields to invoke getattr method
        """
        return self.get_format().format(job)


@flux.util.CLIMain(logger)
def main():
    args = parse_args()

    if args.format:
        fmt = args.format
    else:
        fmt = (
            "{id:>18} {username:<8.8} {name:<10.10} {state:<8.8} "
            "{ntasks:>6} {nnodes_hyphen:>6} {runtime_fsd_hyphen:>8} "
            "{ranks_hyphen}"
        )
    try:
        of = OutputFormat(fmt)
    except ValueError as e:
        raise ValueError("Error in user format: " + str(e))

    jobs = fetch_jobs(args)

    if not args.suppress_header:
        print(of.header())

    for job in jobs:
        print(of.format(job))


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
