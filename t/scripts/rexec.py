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
import sys

import flux


def kill(args):
    h = flux.Flux()
    try:
        payload = {"pid": int(args.pid), "signum": int(args.signum)}
    except ValueError:
        payload = {"pid": -1, "label": args.pid, "signum": int(args.signum)}
    try:
        h.rpc(args.service + ".kill", nodeid=args.rank, payload=payload).get()
    except OSError as exc:
        LOGGER.error(f"kill: {exc}")
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
        print(f"{item['pid']:<8}\t{item['label']}\t{item['cmd']}")


LOGGER = logging.getLogger("rexec")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="rexec")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # kill
    kill_parser = subparsers.add_parser(
        "kill",
        formatter_class=flux.util.help_formatter(),
    )
    kill_parser.add_argument(
        "-r",
        "--rank",
        type=int,
        help="Send RPC to specified broker rank",
        default=flux.constants.FLUX_NODEID_ANY,
    )
    kill_parser.add_argument(
        "-s",
        "--service",
        type=str,
        help="Send RPC to specified service (default rexec)",
        default="rexec",
    )
    kill_parser.add_argument("signum")
    kill_parser.add_argument("pid")
    kill_parser.set_defaults(func=kill)

    # ps
    ps_parser = subparsers.add_parser(
        "ps",
        formatter_class=flux.util.help_formatter(),
    )
    ps_parser.add_argument(
        "-r",
        "--rank",
        type=int,
        help="Send RPC to specified broker rank",
        default=flux.constants.FLUX_NODEID_ANY,
    )
    ps_parser.add_argument(
        "-s",
        "--service",
        type=str,
        help="Send RPC to specified service (default rexec)",
        default="rexec",
    )
    ps_parser.set_defaults(func=ps)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
