#!/bin/false
##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import logging
import sys

import flux
from flux.job import JobID

LOGGER = logging.getLogger("flux-job-exec-override")


def job_exec_start(args):
    """Start testexec job under manual override"""
    try:
        flux.Flux().rpc(
            "job-exec.override", {"event": "start", "jobid": args.jobid}
        ).get()
    except OSError as exc:
        LOGGER.error("%s", exc.strerror)
        sys.exit(1)


def job_exec_finish(args):
    """Finish testexec job under manual override"""
    try:
        flux.Flux().rpc(
            "job-exec.override",
            {"event": "finish", "jobid": args.jobid, "status": args.status},
        ).get()
    except OSError as exc:
        LOGGER.error("%s", exc.strerror)
        sys.exit(1)


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-job-exec")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    start_parser = subparsers.add_parser(
        "start", formatter_class=flux.util.help_formatter()
    )
    start_parser.add_argument("jobid", metavar="JOBID", type=JobID, help="target JOBID")
    start_parser.set_defaults(func=job_exec_start)

    finish_parser = subparsers.add_parser(
        "finish", formatter_class=flux.util.help_formatter()
    )
    finish_parser.add_argument(
        "jobid", metavar="JOBID", type=JobID, help="target JOBID"
    )
    finish_parser.add_argument(
        "status", metavar="STATUS", type=int, help="finish wait status", default=0
    )
    finish_parser.set_defaults(func=job_exec_finish)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
