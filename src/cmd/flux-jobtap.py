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

import os
import sys
import logging
import argparse

import flux
from flux.util import TreedictAction


def jobtap_load(args):
    """Load a jobtap plugin into the job manager"""
    if args.plugin == "none" or args.plugin.startswith("builtin."):
        path = args.plugin
    else:
        path = os.path.abspath(args.plugin)

    try:
        resp = (
            flux.Flux()
            .rpc("job-manager.jobtap", {"load": path, "conf": args.conf})
            .get()
        )
    except FileNotFoundError:
        LOGGER.error(
            "%s not found, no plugin is currently loaded",
            args.plugin,
        )
        sys.exit(1)
    if not args.quiet:
        print("Loaded:")
        for name in resp["plugins"]:
            print(name)
        print("Previously loaded:")
    for name in resp["previous"]:
        print(name)


def jobtap_list(_args):
    """List currently loaded jobtap plugin"""
    resp = flux.Flux().rpc("job-manager.jobtap", {"query_only": True}).get()
    for name in resp["plugins"]:
        print(name)


LOGGER = logging.getLogger("flux-jobtap")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-jobtap")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    load_parser = subparsers.add_parser(
        "load", formatter_class=flux.util.help_formatter()
    )
    load_parser.add_argument(
        "-q",
        "--quiet",
        action="count",
        default=0,
        help="Only print previously loaded plugin after success",
    )
    load_parser.add_argument("plugin", help="Plugin path or builtin name")
    load_parser.add_argument(
        "conf",
        help="List of key=value config keys to set as plugin configuration",
        action=TreedictAction,
        nargs=argparse.REMAINDER,
    )
    load_parser.set_defaults(func=jobtap_load)

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.set_defaults(func=jobtap_list)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
