###############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import errno
import logging
import os
import sys

import flux
import flux.subprocess as subprocess


def exit_with_status(status):
    """
    Given wait status ``status``, exit with an appropriate exit code
    """
    exitcode = 0
    status = int(status)
    if os.WIFEXITED(status):
        exitcode = os.WEXITSTATUS(status)
    elif os.WIFSIGNALED(status):
        exitcode = 128 + os.WTERMSIG(status)
    sys.exit(exitcode)


def pid_or_label(arg):
    """
    Return pid and label where:
    - pid is None if arg is a non integer string
    - label is None if arg is an integer
    Returns:
        pid, label
    """
    try:
        return int(arg), None
    except ValueError:
        # arg is not an integer
        return None, str(arg)


def kill(args):
    wait_rpc = None
    h = flux.Flux()
    pid, label = pid_or_label(args.pid)
    if args.wait:
        # For testing purposes, send wait request before sending kill request:
        wait_rpc = subprocess.wait(
            h, pid=pid, label=label, service=args.service, nodeid=args.rank
        )
    try:
        subprocess.kill(
            h,
            signum=args.signum,
            pid=pid,
            label=label,
            service=args.service,
            nodeid=args.rank,
        ).get()
    except OSError as exc:
        LOGGER.error(f"kill: {exc}")
        # Process may have already been reaped by the wait RPC initiated
        # above, which will cause the kill RPC to fail with ESRCH. In this
        # case fall through to wait statement so program exits with collected
        # wait status (or fails with a wait error in the case the process
        # really could not be found:
        if not (args.wait and exc.errno == errno.ESRCH):
            sys.exit(1)

    if args.wait:
        try:
            exit_with_status(wait_rpc.get_status())
        except OSError as exc:
            LOGGER.error(f"kill: wait failed: {exc}")
            sys.exit(1)


def wait(args):
    h = flux.Flux()
    pid, label = pid_or_label(args.pid)
    try:
        rpc = subprocess.wait(
            h, pid=pid, label=label, service=args.service, nodeid=args.rank
        )
        exit_with_status(rpc.get_status())
    except OSError as exc:
        LOGGER.error(f"wait: {exc}")
        sys.exit(1)


def ps(args):
    headings = {
        "pid": "PID",
        "state": "ST",
        "label": "LABEL",
        "rank": "RANK",
        "cmd": "COMMAND",
    }
    fmt = args.format or "{pid:>9} {state:<2} {label:<12} {cmd}"
    formatter = flux.util.OutputFormat(fmt, headings=headings)

    try:
        procs = subprocess.list(
            flux.Flux(), service=args.service, nodeid=args.rank
        ).get_processes()
    except OSError as exc:
        LOGGER.error(f"ps: {exc}")
        sys.exit(1)
    formatter.print_items(procs, no_header=args.no_header)


LOGGER = logging.getLogger("flux-sproc")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-sproc")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # Parent parser with common arguments
    common_parser = argparse.ArgumentParser(add_help=False)
    common_parser.add_argument(
        "-r",
        "--rank",
        type=int,
        help="Send RPC to specified broker rank",
        default=flux.constants.FLUX_NODEID_ANY,
    )
    common_parser.add_argument(
        "-s",
        "--service",
        type=str,
        help="Send RPC to specified service (default rexec)",
        default="rexec",
    )

    # kill
    kill_parser = subparsers.add_parser(
        "kill",
        parents=[common_parser],
        formatter_class=flux.util.help_formatter(),
    )
    kill_parser.add_argument(
        "-w",
        "--wait",
        action="store_true",
        help="wait for process after sending signal",
    )
    kill_parser.add_argument("signum")
    kill_parser.add_argument("pid")
    kill_parser.set_defaults(func=kill)

    # wait
    wait_parser = subparsers.add_parser(
        "wait",
        parents=[common_parser],
        formatter_class=flux.util.help_formatter(),
    )
    wait_parser.add_argument("pid")
    wait_parser.set_defaults(func=wait)

    # ps
    ps_parser = subparsers.add_parser(
        "ps",
        parents=[common_parser],
        formatter_class=flux.util.help_formatter(),
    )
    ps_parser.add_argument(
        "-o",
        "--format",
        help="Specify output format using Python's string format syntax",
    )
    ps_parser.add_argument(
        "-n",
        "--no-header",
        action="store_true",
        help="Suppress header output",
    )
    ps_parser.set_defaults(func=ps)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
