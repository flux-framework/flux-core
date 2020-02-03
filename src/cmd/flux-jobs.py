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


def runtime(job, roundup):
    if "t_cleanup" in job and "t_run" in job:
        t = job["t_cleanup"] - job["t_run"]
        if roundup:
            t = round(t + 0.5)
    elif "t_run" in job:
        t = time.time() - job["t_run"]
        if roundup:
            t = round(t + 0.5)
    else:
        t = 0.0
    return t


def runtime_fsd(job, hyphenifzero):
    t = runtime(job, False)
    #  Round <1ms down to 0s for now
    if t < 1.0e-3:
        s = "0s"
    elif t < 10.0:
        s = "%.03fs" % t
    elif t < 60.0:
        s = "%.2gs" % t
    elif t < (60.0 * 60.0):
        s = "%.2gm" % (t / 60.0)
    elif t < (60.0 * 60.0 * 24.0):
        s = "%.2gh" % (t / (60.0 * 60.0))
    else:
        s = "%.2gd" % (t / (60.0 * 60.0 * 24.0))
    if hyphenifzero and s == "0s":
        return "-"
    return s


def runtime_hms(job):
    t = runtime(job, True)
    return str(timedelta(seconds=t))


def statetostr(job, singlechar):
    return raw.flux_job_statetostr(job["state"], singlechar).decode("utf-8")


def job_username(job):
    try:
        return pwd.getpwuid(job["userid"]).pw_name
    except KeyError:
        return str(job["userid"])


def output_format(fmt, jobs):
    for job in jobs:
        s = fmt.format(
            id=job["id"],
            userid=job["userid"],
            username=job_username(job),
            priority=job["priority"],
            state=statetostr(job, False),
            state_single=statetostr(job, True),
            name=job["name"],
            ntasks=job["ntasks"],
            nnodes=job.get("nnodes", ""),
            nnodes_hyphen=job.get("nnodes", "-"),
            ranks=job.get("ranks", ""),
            ranks_hyphen=job.get("ranks", "-"),
            t_submit=job["t_submit"],
            t_depend=job["t_depend"],
            t_sched=job.get("t_sched", 0.0),
            t_run=job.get("t_run", 0.0),
            t_cleanup=job.get("t_cleanup", 0.0),
            t_inactive=job.get("t_inactive", 0.0),
            runtime=runtime(job, False),
            runtime_fsd=runtime_fsd(job, False),
            runtime_fsd_hyphen=runtime_fsd(job, True),
            runtime_hms=runtime_hms(job),
        )
        print(s)


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

    rpc_handle = flux.job.job_list(h, args.count, attrs, userid, states)
    try:
        jobs = flux.job.job_list_get(rpc_handle)
    except EnvironmentError as e:
        print("{}: {}".format("rpc", e.strerror), file=sys.stderr)
        sys.exit(1)

    return jobs


def fetch_jobs(args):
    if args.from_stdin:
        return fetch_jobs_stdin(args)
    return fetch_jobs_flux(args)


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
    parser.add_argument(
        "--from-stdin", action="store_true", help=argparse.SUPPRESS,
    )
    return parser.parse_args()


@flux.util.CLIMain(logger)
def main():
    args = parse_args()
    jobs = fetch_jobs(args)

    if args.format:
        output_format(args.format, jobs)
    else:
        fmt = (
            "{id:>18} {username:<8.8} {name:<10.10} {state:<8.8} "
            "{ntasks:>6} {runtime_fsd_hyphen}"
        )
        if not args.suppress_header:
            s = fmt.format(
                id="JOBID",
                username="USER",
                name="NAME",
                state="STATE",
                ntasks="NTASKS",
                runtime_fsd_hyphen="RUNTIME",
            )
            print(s)
        output_format(fmt, jobs)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
