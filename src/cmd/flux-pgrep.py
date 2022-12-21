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
import json
import logging
import os
import re
import sys
from datetime import datetime
from itertools import islice
from pathlib import PurePath

import flux
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.job import Constraint, JobID, JobInfoFormat, JobList
from flux.util import UtilConfig, parse_datetime, parse_fsd

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
        super().__init__(name="flux-pgrep", initial_dict=initial_dict)

    def validate(self, path, config):
        """Validate a loaded flux-pgrep config file as dictionary"""
        for key, value in config.items():
            if key == "formats":
                self.validate_formats(path, value)
            else:
                raise ValueError(f"{path}: invalid key {key}")


class PgrepConstraint(Constraint):
    """
    Build constraint object from string, overriding operators so that
    'name' (match on job name) is the default operator. Also include
    shorthand for a select few other operators in the context of matching
    jobs
    """

    operator_map = {
        None: "name",
        "id": "jobid",
        "host": "hostlist",
        "rank": "ranks",
    }


class JobPgrep:
    def __init__(self, args):
        self.cache = {}
        self.query_string = self.convert_terms(args)
        self.constraint = PgrepConstraint(self.query_string)
        self.validate(self.constraint)

    def dumps(self):
        return json.dumps(self.constraint)

    def convert_terms(self, args):
        """Convert some shorthand terms into official op:args arguments"""
        result = []
        for arg in self.join_terms(args):
            if arg.startswith("@"):
                #  A leading '@' is converted to query for jobs that were
                #  running at a given time (@5pm) or time range (@8am..noon)
                arg = arg[1:]
                if ".." in arg:
                    # Jobs that were running in a time range between start,end
                    start, end = arg.split("..")
                    arg = "(not ("
                    if start:
                        arg += f"end:<{start}"
                    if start and end:
                        arg += " or "
                    if end:
                        arg += f"start:>{end}"
                    arg += "))"
                else:
                    # Jobs running at a single point in time:
                    arg = f"(start:<={arg} and end:>={arg})"
            elif ":" not in arg and ".." in arg:
                #  X..Y with no leading @ or without : is translated to a
                #   range of jobids.
                arg = f"jobid:{arg}"
            result.append(arg)
        return " ".join(result)

    def join_terms(self, args):
        """
        Join terms in args into a Constraint expression.
        If two terms are joined without `and` or `or` then insert `and`
        """
        lst = []
        for i in range(1, len(args)):
            (prev, cur) = (args[i - 1], args[i])
            lst.append(prev)
            if prev not in ["and", "or", "not"] and cur not in ["and", "or"]:
                lst.append("and")
        lst.append(args[-1])
        return lst

    def validate(self, constraint):
        """
        Validate a Pgrep constraint dict
        """
        for op, args in constraint.items():
            if op == "name":
                self.validate_name(args)
            elif op == "jobid":
                self.validate_jobid(args)
            elif op == "status":
                self.validate_status(args)
            elif op == "ranks":
                self.validate_ranks(args)
            elif op == "hostlist":
                self.validate_hostlist(args)
            elif op in ["runtime", "start", "end"]:
                self.validate_one_arg(op, args)
            elif op == "start":
                self.validate_start(args)
            elif op == "end":
                self.validate_end(args)
            elif op == "is":
                self.validate_is(args)
            elif op == "nnodes":
                pass
            elif op in ["before", "since"]:
                self.validate_before_since(op, args)
            elif op in ["or", "and", "not"]:
                self.validate_conditional(op, args)
            else:
                raise ValueError(f"unknown match operator: {op}")

    def validate_conditional(self, op, args):
        for constraint in args:
            self.validate(constraint)

    def validate_name(self, args):
        for value in args:
            if value not in self.cache:
                regex = re.compile(value)
                self.cache[value] = regex

    def validate_one_arg(self, op, args):
        if len(args) > 1:
            raise ValueError(f"{op} should have only a single arg")

    def validate_jobid(self, args):
        for jobid in args:
            if jobid not in self.cache:
                self.cache[jobid] = JobID(jobid)

    def validate_ranks(self, args):
        key = ",".join(args)
        if key not in self.cache:
            self.cache[key] = IDset(key)

    def validate_hostlist(self, args):
        key = ",".join(args)
        if key not in self.cache:
            self.cache[key] = Hostlist(key)

    def validate_before_since(self, op, args):
        if len(args) > 1:
            raise ValueError(f"{op} only takes a single term")
        self.cache[args[0]] = parse_datetime(args[0], assumeFuture=False)

    def validate_is(self, args):
        for arg in args:
            if arg.lower() not in [
                "pending",
                "depend",
                "priority",
                "sched",
                "run",
                "running",
                "cleanup",
                "inactive",
                "failure",
                "failed",
                "canceled",
                "timeout",
                "success",
                "complete",
                "started",
            ]:
                raise ValueError("is: invalid argument '{arg}'")

    def match_is(self, job, args):
        for arg in args:
            arg = arg.lower()
            state = job.state.lower()
            if arg == "started" and job.runtime:
                return True
            if arg == "running" and state in ["run", "cleanup"]:
                arg = "run"
            if arg == "pending" and state in ["depend", "priority", "sched"]:
                return True
            if arg == "complete" and state in ["cleanup", "inactive"]:
                return True
            if arg == state:
                return True
            if job.result and arg == job.result.lower():
                return True
            if arg == "success" and job.success:
                return True
            if arg == "failure" and not job.success:
                return True
        return False

    def match_name(self, job, args):
        for arg in args:
            if not self.cache[arg].search(job.name):
                return False
        return True

    def match_ranks(self, job, args):
        key = ",".join(args)
        result = self.cache[key].intersect(job.ranks)
        return result.count() > 0

    def match_hostlist(self, job, args):
        key = ",".join(args)
        hl = self.cache[key]
        for host in Hostlist(job.nodelist):
            if host in hl:
                return True
        return False

    def match_before(self, job, args):
        before = self.cache[args[0]]
        time = datetime.fromtimestamp(job.t_submit).astimezone()
        return time < before

    def match_since(self, job, args):
        since = self.cache[args[0]]
        time = datetime.fromtimestamp(job.t_submit).astimezone()
        return time > since

    def conditional(self, value, arg, conversion_function):
        """
        Handle a generic conditional expression, i.e. one that
        starts with ">=", "<=", ">", "<".
        """

        def conv(value):
            if value not in self.cache:
                self.cache[value] = conversion_function(value)
            return self.cache[value]

        val = conv(value)
        if arg.startswith(">=") or arg.startswith("+="):
            return val >= conv(arg[2:])
        if arg.startswith("<=") or arg.startswith("-="):
            return val <= conv(arg[2:])
        if arg.startswith("<") or arg.startswith("-"):
            return val < conv(arg[1:])
        if arg.startswith(">") or arg.startswith("+"):
            return val > conv(arg[1:])
        if arg.startswith("=="):
            arg = arg[2:]
        if ".." in arg:
            start, end = arg.split("..")
            if start and end:
                return conv(start) <= val <= conv(end)
            elif start:
                return conv(start) <= val
            elif end:
                return val <= conv(end)
        return val == conv(arg)

    def convert_duration(self, duration):
        if isinstance(duration, str):
            time = parse_fsd(duration)
        elif isinstance(duration, (float, int)):
            time = float(duration)
        return time

    def match_runtime(self, job, args):
        return self.conditional(job.runtime, args[0], self.convert_duration)

    def convert_datetime(self, dt):
        if isinstance(dt, (float, int)):
            time = datetime.fromtimestamp(dt).astimezone()
        else:
            time = parse_datetime(dt, assumeFuture=False)
        return time

    def match_start(self, job, args):
        return self.conditional(job.t_run, args[0], self.convert_datetime)

    def match_end(self, job, args):
        return self.conditional(job.t_cleanup, args[0], self.convert_datetime)

    def match_nnodes(self, job, args):
        return self.conditional(job.nnodes, args[0], int)

    def convert_jobid(self, jobid):
        try:
            return self.cache[jobid]
        except KeyError:
            return JobID(jobid)

    def match_jobid(self, job, args):
        return self.conditional(job.id, args[0], self.convert_jobid)

    def match_and(self, job, args):
        for constraint in args:
            if not self.match(job, constraint):
                return False
        return True

    def match_or(self, job, args):
        for constraint in args:
            if self.match(job, constraint):
                return True
        return False

    def match_not(self, job, args):
        return not self.match_and(job, args)

    def match(self, job, constraint=None):
        if constraint is None:
            constraint = self.constraint

        for op, args in constraint.items():
            result = False
            if op == "name":
                result = self.match_name(job, args)
            elif op == "jobid":
                result = self.match_jobid(job, args)
            elif op == "ranks":
                result = self.match_ranks(job, args)
            elif op == "hostlist":
                result = self.match_hostlist(job, args)
            elif op == "since":
                result = self.match_since(job, args)
            elif op == "before":
                result = self.match_before(job, args)
            elif op == "range":
                result = self.match_range(job, args)
            elif op == "runtime":
                result = self.match_runtime(job, args)
            elif op == "start":
                result = self.match_start(job, args)
            elif op == "end":
                result = self.match_end(job, args)
            elif op == "is":
                result = self.match_is(job, args)
            elif op == "nnodes":
                result = self.match_nnodes(job, args)
            elif op == "and":
                result = self.match_and(job, args)
            elif op == "not":
                result = self.match_not(job, args)
            elif op == "or":
                result = self.match_or(job, args)

            # Multiple keys is equivalent to "and"
            if not result:
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
    if PROGRAM == "flux-pgrep":
        parser.add_argument(
            "-n",
            "--suppress-header",
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
        parser.add_argument(
            "-k",
            "--kill",
            action="store_true",
            help="Cancel instead of printing matching jobs",
        )
    parser.add_argument(
        "--wait", action="store_true", help="Wait for jobs to finish after cancel"
    )
    parser.add_argument(
        "expression",
        metavar="EXPRESSION",
        type=str,
        nargs="+",
        help="job name pattern or match expression",
    )
    return parser.parse_args()


def pkill(fh, args, jobs):
    success = 0
    exitcode = 0

    def wait_cb(future, job):
        future.get_dict()

    def cancel_cb(future, args, job):
        nonlocal success
        nonlocal exitcode
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
    if PROGRAM == "flux-pkill":
        args.kill = True

    fh = flux.Flux()
    pgrep = JobPgrep(args.expression)
    jobs = list(islice(filter(pgrep.match, fetch_jobs(args, fh)), args.count))

    if args.kill:
        pkill(fh, args, jobs)

    fmt = FluxPgrepConfig().load().get_format_string(args.format)
    try:
        formatter = JobInfoFormat(fmt)
    except ValueError as err:
        raise ValueError("Error in user format: " + str(err))

    sformatter = JobInfoFormat(formatter.filter_empty(jobs))

    if args.format == "default":
        args.suppress_header = True

    sformatter.print_items(jobs, no_header=args.suppress_header)
