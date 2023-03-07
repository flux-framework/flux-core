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
import pwd
import sys
from operator import itemgetter

import flux
from flux.job import cancel_async

LOGGER = logging.getLogger("flux-cancel")


def parse_args():
    description = """
    Cancel pending or running jobs with an optional exception note.
    """
    parser = argparse.ArgumentParser(
        prog="flux-cancel",
        usage="flux cancel [OPTIONS] [JOBID...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
    )
    parser.add_argument("--all", action="store_true", help="Target all jobs")
    parser.add_argument(
        "-n", "--dry-run", action="store_true", help="Do not cancel any jobs"
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="Suppress output when no jobs match",
    )
    parser.add_argument(
        "-u",
        "--user",
        type=str,
        metavar="USER",
        help="Set target user or 'all' (instance owner only)",
    )
    parser.add_argument(
        "-S",
        "--states",
        action="append",
        help="List of job states to target (default=active)",
    )
    parser.add_argument(
        "-m",
        "--message",
        type=str,
        metavar="NOTE",
        help="Set optional cancel exception note",
    )
    parser.add_argument(
        "jobids",
        metavar="JOBID...",
        type=flux.job.JobID,
        nargs="*",
        help="jobids to cancel",
    )
    return parser.parse_args()


def log(msg):
    """Log to stderr without logging INFO prefix"""
    print(f"flux-cancel: {msg}", file=sys.stderr)


def cancel(args):
    if not args.jobids:
        raise ValueError("No target jobs were provided")
    if args.dry_run:
        count = len(args.jobids)
        log(f"Would cancel {count} job{'s'[:count^1]}")
        sys.exit(0)
    h = flux.Flux()
    futures = {x: cancel_async(h, x, args.message) for x in args.jobids}
    errors = 0
    for jobid, future in futures.items():
        try:
            future.get()
        except OSError as exc:
            errors += 1
            LOGGER.error(f"{jobid}: {exc.strerror}")
    if errors > 0:
        sys.exit(1)


def cancelall(args):

    STATES = {
        "depend": flux.constants.FLUX_JOB_STATE_DEPEND,
        "priority": flux.constants.FLUX_JOB_STATE_PRIORITY,
        "sched": flux.constants.FLUX_JOB_STATE_SCHED,
        "run": flux.constants.FLUX_JOB_STATE_RUN,
        "pending": flux.constants.FLUX_JOB_STATE_PENDING,
        "running": flux.constants.FLUX_JOB_STATE_RUNNING,
        "active": flux.constants.FLUX_JOB_STATE_ACTIVE,
    }

    if args.user is None:
        userid = os.getuid()
    elif args.user == "all":
        userid = flux.constants.FLUX_USERID_UNKNOWN
    else:
        try:
            userid = pwd.getpwnam(args.user).pw_uid
        except KeyError:
            try:
                userid = int(args.user)
            except ValueError:
                raise ValueError(f"Invalid user {args.user} specified")

    statemask = 0
    if args.states:
        for state in ",".join(args.states).split(","):
            try:
                statemask |= STATES[state.lower()]
            except KeyError:
                raise ValueError(f"Invalid state {state} specified")
    else:
        statemask = STATES["pending"] | STATES["running"]

    payload = {
        "dry_run": args.dry_run,
        "userid": userid,
        "states": statemask,
        "severity": 0,
        "type": "cancel",
    }
    if args.message is not None:
        payload["note"] = args.message

    count, errors = itemgetter("count", "errors")(
        flux.Flux().rpc("job-manager.raiseall", payload).get()
    )

    if count > 0:
        msg = f"{count} job{'s'[:count^1]}"
        if args.dry_run:
            log(f"Would cancel {msg}")
        else:
            log(f"Canceled {msg} ({errors} error{'s'[:errors^1]})")
    elif not args.quiet:
        log("Matched 0 jobs")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    args = parse_args()

    if args.user or args.states:
        args.all = True

    if args.jobids and args.all:
        LOGGER.error(
            "Do not specify a list of jobids with --all,--user,--states options"
        )
        sys.exit(1)

    if args.all:
        cancelall(args)
    else:
        cancel(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
