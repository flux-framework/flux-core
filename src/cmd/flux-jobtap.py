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


def jobtap_remove(args):
    """Remove jobtap plugin matching name"""
    try:
        resp = flux.Flux().rpc("job-manager.jobtap", {"remove": args.plugin})
    except FileNotFoundError:
        LOGGER.error("%s not found", args.plugin)
        sys.exit(1)


def jobtap_load(args):
    """Load a jobtap plugin into the job manager"""
    if args.plugin.startswith("builtin."):
        path = args.plugin
    else:
        path = os.path.abspath(args.plugin)

    req = {"load": path}
    if args.conf:
        req["conf"] = args.conf
    if args.remove:
        req["remove"] = args.remove

    try:
        resp = flux.Flux().rpc("job-manager.jobtap", req).get()
    except FileNotFoundError:
        LOGGER.error(
            "%s not found",
            args.plugin,
        )
        sys.exit(1)


def jobtap_list(_args):
    """List currently loaded jobtap plugins"""
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
        "-r",
        "--remove",
        metavar="NAME",
        help="Remove plugin NAME before loading new plugin. "
        + "NAME may optionally be a shell glob pattern which removes all "
        + 'matching plugins. ("all" is a synonym for "*")',
    )
    load_parser.add_argument("plugin", help="Plugin path or builtin name")
    load_parser.add_argument(
        "conf",
        help="List of key=value config keys to set as plugin configuration",
        action=TreedictAction,
        nargs=argparse.REMAINDER,
    )
    load_parser.set_defaults(func=jobtap_load)

    remove_parser = subparsers.add_parser(
        "remove", formatter_class=flux.util.help_formatter()
    )
    remove_parser.add_argument(
        "plugin",
        help="Plugin name or pattern to remove. "
        + '"all" may be used to remove all loaded plugins',
    )
    remove_parser.set_defaults(func=jobtap_remove)

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.set_defaults(func=jobtap_list)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
