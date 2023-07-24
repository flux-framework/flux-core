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
import sys

import flux
from flux.job import JobID, JobList
from flux.job.watcher import JobWatcher
from flux.util import (
    FilterAction,
    FilterActionSetUpdate,
    FilterTrueAction,
    help_formatter,
)


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-watch", formatter_class=help_formatter()
    )
    parser.add_argument(
        "-a",
        "--active",
        action=FilterTrueAction,
        help="Watch all active jobs for current user",
    )
    parser.add_argument(
        "-A",
        "--all",
        action=FilterTrueAction,
        help="Watch all jobs for current user in all states including inactive",
    )
    parser.add_argument(
        "-c",
        "--count",
        action=FilterAction,
        type=int,
        metavar="N",
        default=0,
        help="Limit to N jobs (default 1000)",
    )
    parser.add_argument(
        "-f",
        "--filter",
        action=FilterActionSetUpdate,
        metavar="STATE|RESULT",
        default=set(),
        help="Watch jobs with specific job state or result",
    )
    parser.add_argument(
        "--since",
        action=FilterAction,
        type=str,
        metavar="WHEN",
        default=0.0,
        help="Include only jobs that have been inactive since WHEN. "
        + "(implies -a if no other --filter option is specified)",
    )
    parser.add_argument(
        "--progress",
        action="store_true",
        default=False,
        help="Show progress bar",
    )
    parser.add_argument(
        "--jps",
        action="store_true",
        default=False,
        help="Display jobs/s instead of elapsed time ",
    )
    parser.add_argument(
        "-l",
        "--label-io",
        action="store_true",
        default=False,
        help="label lines of output with task id",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Increase verbosity",
    )
    parser.add_argument(
        "jobids",
        metavar="JOBID",
        type=JobID,
        nargs="*",
        help="Watch specific jobids",
    )
    parser.set_defaults(filtered=False)
    return parser.parse_args()


LOGGER = logging.getLogger("flux-watch")


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    starttime = None
    since = 0.0

    args = parse_args()

    if args.jobids and args.filtered:
        LOGGER.warning("Filtering options ignored with jobid list")

    fh = flux.Flux()
    instance_starttime = float(fh.attr_get("broker.starttime"))

    if args.since:
        #  Implies --all unless any other filter option is already in effect.
        if not args.filter:
            args.all = True
        try:
            since = flux.util.parse_datetime(args.since).timestamp()
        except ValueError:
            try:
                since = float(args.since)
            except ValueError:
                LOGGER.error(f"--since: invalid value '{args.since}'")
                sys.exit(2)

        # Ensure args.since is in the past
        if since > flux.util.parse_datetime("now").timestamp():
            LOGGER.error("--since=%s appears to be in the future", args.since)
            sys.exit(2)

        #  With since, start elapsed timer at maximum of since time or
        #  instance starttime
        starttime = max(since, instance_starttime)

    if args.active:
        args.filter.update(("pending", "running"))

    if args.all:
        args.filter.update(("pending", "running", "inactive"))
        if not starttime:
            starttime = instance_starttime

    if not args.filter and not args.jobids:
        LOGGER.error("At least one job selection option is required")
        print("Try 'flux watch --help' for more information.", file=sys.stderr)
        sys.exit(1)

    joblist = JobList(
        fh,
        ids=args.jobids,
        attrs=["state", "result", "t_submit"],
        filters=args.filter,
        since=since,
        user=str(os.getuid()),
        max_entries=args.count,
    )
    jobs = list(joblist.jobs())
    for errmsg in joblist.errors:
        LOGGER.error(errmsg)

    if not jobs:
        LOGGER.info("No matching jobs")
        sys.exit(0)

    if args.verbose:
        LOGGER.info(f"Watching {len(jobs)} jobs")

    if args.progress and not sys.stdout.isatty():
        LOGGER.warning("stdout is not a tty. Ignoring --progress option")
        args.progress = False

    watcher = JobWatcher(
        fh,
        jobs=jobs,
        progress=args.progress,
        jps=args.jps,
        log_events=(args.verbose > 1),
        log_status=(args.verbose > 0),
        labelio=args.label_io,
        starttime=starttime,
    ).start()

    fh.reactor_run()

    sys.exit(watcher.exitcode)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
