##############################################################
# Copyright 2020 Lawrence Livermore National Security, LLC
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
from flux.rpc import RPC


def cleanup_push(args):
    """
    Add a command to run after completion of the initial program, before rc3.
    It is pushed to the front of the list of commands.

    If command was not provided as args, read one command per line from
    stdio.  Push these in reverse order to retain the order of the block of
    commands.
    """
    if args.cmdline:
        commands = [(" ".join(args.cmdline))]
    else:
        commands = [line.strip() for line in sys.stdin]

    RPC(
        flux.Flux(),
        "runat.push",
        {"name": "cleanup", "commands": commands[::-1]},
    ).get()


LOGGER = logging.getLogger("flux-admin")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-admin")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    cleanup_push_parser = subparsers.add_parser(
        "cleanup-push", formatter_class=flux.util.help_formatter()
    )
    cleanup_push_parser.add_argument(
        "cmdline", help="Command line", nargs=argparse.REMAINDER
    )
    cleanup_push_parser.set_defaults(func=cleanup_push)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
