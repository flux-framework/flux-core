###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# startctl - tell flux-start to do things

import argparse
import logging
import os
import sys

import flux


def kill(args):
    h = flux.Flux(os.environ.get("FLUX_START_URI"))
    try:
        h.rpc("start.kill", {"rank": int(args.rank), "signum": int(args.signum)}).get()
    except ProcessLookupError:
        LOGGER.error("rank %s broker process not found", args.rank)
        sys.exit(1)


def run(args):
    h = flux.Flux(os.environ.get("FLUX_START_URI"))
    h.rpc("start.run", {"rank": int(args.rank)}).get()


def wait(args):
    h = flux.Flux(os.environ.get("FLUX_START_URI"))
    resp = h.rpc("start.wait", {"rank": int(args.rank)}).get()
    sys.exit(int(resp["exit_rc"]))


def status(args):
    h = flux.Flux(os.environ.get("FLUX_START_URI"))
    print(h.rpc("start.status").get_str())


LOGGER = logging.getLogger("startctl")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="startctl")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # status
    status_parser = subparsers.add_parser(
        "status",
        formatter_class=flux.util.help_formatter(),
    )
    status_parser.set_defaults(func=status)

    # kill
    kill_parser = subparsers.add_parser(
        "kill",
        usage="startctl kill rank signum",
        formatter_class=flux.util.help_formatter(),
    )
    kill_parser.add_argument("rank")
    kill_parser.add_argument("signum")
    kill_parser.set_defaults(func=kill)

    # run
    run_parser = subparsers.add_parser(
        "run",
        usage="startctl run rank",
        formatter_class=flux.util.help_formatter(),
    )
    run_parser.add_argument("rank")
    run_parser.set_defaults(func=run)

    # wait
    wait_parser = subparsers.add_parser(
        "wait",
        usage="startctl wait rank",
        formatter_class=flux.util.help_formatter(),
    )
    wait_parser.add_argument("rank")
    wait_parser.set_defaults(func=wait)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
