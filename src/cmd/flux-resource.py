##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import sys
import logging
import argparse
import os.path
import json

import flux
from flux.resource import ResourceSet
from flux.rpc import RPC
from flux.memoized_property import memoized_property


def reload(args):
    """
    Send a "reload" request to resource module
    """
    RPC(
        flux.Flux(),
        "resource.reload",
        {"path": os.path.realpath(args.path), "xml": args.xml, "force": args.force},
        nodeid=0,
    ).get()


def drain(args):
    """
    Send a drain request to resource module for args.targets
    """
    RPC(
        flux.Flux(),
        "resource.drain",
        {"targets": args.targets, "reason": " ".join(args.reason)},
    ).get()


def undrain(args):
    """
    Send an "undrain" request to resource module for args.targets
    """
    RPC(flux.Flux(), "resource.undrain", {"targets": args.targets}).get()


class SchedResourceList:
    """
    Encapsulate response from sched.resource-status query.
    The response will contain 3 Rv1 resource sets:
        "all"       - all resources known to scheduler
        "down"      - resources currently unavailable (drained or down)
        "allocated" - resources currently allocated to jobs

    From these sets, the "up" and "free" resource sets are
    computed on-demand.

    """

    def __init__(self, resp):
        for state in ["all", "down", "allocated"]:
            rset = ResourceSet(resp.get(state))
            rset.state = state
            setattr(self, f"_{state}", rset)

    def __getattr__(self, attr):
        if attr.startswith("_"):
            raise AttributeError
        try:
            return getattr(self, f"_{attr}")
        except KeyError:
            raise AttributeError(f"Invalid SchedResourceList attr {attr}")

    #  Make class subscriptable, e.g. resources[state]
    def __getitem__(self, item):
        return getattr(self, item)

    @memoized_property
    # pylint: disable=invalid-name
    def up(self):
        res = self.all - self.down
        res.state = "up"
        return res

    @memoized_property
    def free(self):
        res = self.up - self.allocated
        res.state = "free"
        return res


def list_handler(args):
    valid_states = ["up", "down", "allocated", "free", "all"]
    headings = {
        "state": "STATE",
        "nnodes": "NNODES",
        "ncores": "NCORES",
        "ngpus": "NGPUS",
        "ranks": "RANKS",
        "rlist": "LIST",
    }

    states = args.states.split(",")
    for state in states:
        if state not in valid_states:
            LOGGER.error("Invalid resource state %s specified", state)
            sys.exit(1)

    fmt = "{state:>10} {nnodes:>6} {ncores:>8} {ngpus:>8}"
    if args.verbose:
        fmt += " {rlist}"
    if args.format:
        fmt = args.format

    formatter = flux.util.OutputFormat(headings, fmt, prepend="0.")

    if args.from_stdin:
        resp = json.load(sys.stdin)
    else:
        resp = RPC(flux.Flux(), "sched.resource-status").get()
    resources = SchedResourceList(resp)

    if not args.no_header:
        print(formatter.header())
    for state in states:
        print(formatter.format(resources[state]))


LOGGER = logging.getLogger("flux-resource")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="flux-resource")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    drain_parser = subparsers.add_parser(
        "drain", formatter_class=flux.util.help_formatter()
    )
    drain_parser.add_argument(
        "targets", help="List of targets to drain (IDSET or HOSTLIST)"
    )
    drain_parser.add_argument("reason", help="Reason", nargs=argparse.REMAINDER)
    drain_parser.set_defaults(func=drain)

    undrain_parser = subparsers.add_parser(
        "undrain", formatter_class=flux.util.help_formatter()
    )
    undrain_parser.add_argument(
        "targets", help="List of targets to resume (IDSET or HOSTLIST)"
    )
    undrain_parser.set_defaults(func=undrain)

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Include lists of allocated/free resources",
    )
    list_parser.add_argument(
        "-o",
        "--format",
        help="Specify output format using Python's string format syntax",
    )
    list_parser.add_argument(
        "-s",
        "--states",
        metavar="STATE,...",
        default="free,allocated,down",
        help="Output resources in given states",
    )
    list_parser.add_argument(
        "-n", "--no-header", action="store_true", help="Suppress header output"
    )
    list_parser.add_argument(
        "--from-stdin", action="store_true", help=argparse.SUPPRESS
    )
    list_parser.set_defaults(func=list_handler)

    reload_parser = subparsers.add_parser(
        "reload", formatter_class=flux.util.help_formatter()
    )
    reload_parser.set_defaults(func=reload)
    reload_parser.add_argument("path", help="path to R or hwloc <rank>.xml dir")
    reload_parser.add_argument(
        "-x",
        "--xml",
        action="store_true",
        default=False,
        help="interpret path as XML dir",
    )
    reload_parser.add_argument(
        "-f",
        "--force",
        action="store_true",
        default=False,
        help="allow resources to contain invalid ranks",
    )

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
