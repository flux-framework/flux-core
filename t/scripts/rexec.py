###############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# rexec - bare bones rexec client

import argparse
import logging
import os
import sys

import flux


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


def kill(args):
    h = flux.Flux()
    try:
        payload = {"pid": int(args.pid), "signum": int(args.signum)}
    except ValueError:
        payload = {"pid": -1, "label": args.pid, "signum": int(args.signum)}
    if args.wait:
        # For testing purposes, send wait request before sending kill request:
        wait_f = h.rpc(
            args.service + ".wait",
            nodeid=args.rank,
            payload={x: payload[x] for x in ("pid", "label") if x in payload},
        )
    try:
        h.rpc(args.service + ".kill", nodeid=args.rank, payload=payload).get()
    except OSError as exc:
        LOGGER.error(f"kill: {exc}")
        sys.exit(1)
    if args.wait:
        try:
            exit_with_status(wait_f.get()["status"])
        except OSError as exc:
            LOGGER.error(f"kill: wait failed: {exc}")
            sys.exit(1)


def wait(args):
    h = flux.Flux()
    try:
        payload = {"pid": int(args.pid)}
    except ValueError:
        payload = {"pid": -1, "label": args.pid}
    try:
        exit_with_status(
            h.rpc(args.service + ".wait", nodeid=args.rank, payload=payload).get()[
                "status"
            ]
        )
    except OSError as exc:
        LOGGER.error(f"wait: {exc}")
        sys.exit(1)


def ps(args):
    h = flux.Flux()
    try:
        resp = h.rpc(
            args.service + ".list",
            nodeid=int(args.rank),
        ).get()
    except OSError as exc:
        LOGGER.error(f"ps: {exc}")
        sys.exit(1)
    for item in resp["procs"]:
        if not item["label"]:
            item["label"] = "-"
        print(f"{item['pid']:<8}\t{item['state']}\t{item['label']:<10}\t{item['cmd']}")


LOGGER = logging.getLogger("rexec")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="rexec")
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
    ps_parser.set_defaults(func=ps)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
