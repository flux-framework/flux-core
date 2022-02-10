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
from datetime import datetime

import flux
from flux.idset import IDset
from flux.hostlist import Hostlist
from flux.resource import ResourceSet, resource_list, SchedResourceList
from flux.rpc import RPC


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
    Send a drain request to resource module for args.targets, if args.targets
    not specified, then list currently drained targets
    """
    if args.targets is None:
        drain_list()
        return
    payload = {
        "targets": args.targets,
    }
    if args.update and args.force:
        LOGGER.error("Only one of --force and --update may be specified")
        sys.exit(1)
    if args.update:
        payload["mode"] = "update"
    elif args.force:
        payload["mode"] = "overwrite"
    if args.reason:
        payload["reason"] = " ".join(args.reason)
    RPC(
        flux.Flux(),
        "resource.drain",
        payload,
        nodeid=0,
    ).get()


def undrain(args):
    """
    Send an "undrain" request to resource module for args.targets
    """
    RPC(flux.Flux(), "resource.undrain", {"targets": args.targets}, nodeid=0).get()


class StatusLine:
    """Information specific to a given flux resource status line"""

    def __init__(self, state, ranks, hosts, reason=None, timestamp=None):
        self.state = state
        self.hostlist = hosts
        self.rank_idset = ranks
        if reason is None:
            reason = ""
        self.reason = reason
        if timestamp:
            self.timestamp = datetime.fromtimestamp(timestamp).strftime("%FT%T")
        else:
            self.timestamp = None

    def update(self, ranks, hosts):
        self.rank_idset.add(ranks)
        self.hostlist.append(hosts)
        self.hostlist.sort()

    @property
    def nodelist(self):
        return str(self.hostlist)

    @property
    def ranks(self):
        return str(self.rank_idset)

    @property
    def nnodes(self):
        return len(self.hostlist)


def drain_list():
    headings = {
        "timestamp": "TIMESTAMP",
        "ranks": "RANK",
        "reason": "REASON",
        "nodelist": "NODELIST",
    }
    resp = RPC(flux.Flux(), "resource.status", nodeid=0).get()
    rset = ResourceSet(resp["R"])
    nodelist = rset.nodelist

    lines = []
    for ranks, entry in resp["drain"].items():
        ranks = IDset(ranks)
        line = StatusLine(
            "drain",
            ranks,
            Hostlist([nodelist[i] for i in ranks]),
            entry["reason"],
            entry["timestamp"],
        )
        lines.append(line)

    fmt = "{timestamp:<20} {ranks:<8} {reason:<30} {nodelist}"
    formatter = flux.util.OutputFormat(headings, fmt, prepend="0.")
    print(formatter.header())
    for line in lines:
        print(formatter.format(line))


class ResourceStatus:
    """Container for resource.status RPC response"""

    # pylint: disable=too-many-instance-attributes

    def __init__(self, rset=None):
        self.rset = ResourceSet(rset)
        self.nodelist = self.rset.nodelist
        self.all = self.rset.ranks
        self.statuslines = []
        self.bystate = {}
        self.idsets = {}

    def __iter__(self):
        for line in self.statuslines:
            yield line

    @property
    def avail(self):
        avail = self.all.copy()
        avail.subtract(self.idsets["offline"])
        avail.subtract(self.idsets["drain"])
        avail.subtract(self.idsets["exclude"])
        return avail

    def _idset_update(self, state, idset):
        if state not in self.idsets:
            self.idsets[state] = IDset()
        self.idsets[state].add(idset)

    def find(self, state, reason=""):
        try:
            return self.bystate[f"{state}:{reason}"]
        except KeyError:
            return None

    def append(self, state, ranks="", reason=None):
        #
        # If an existing status line has matching state and reason
        #  update instead of appending a new output line:
        #  (mainly useful for "drain" when reasons are not displayed)
        #
        hosts = Hostlist([self.nodelist[i] for i in ranks])
        rstatus = self.find(state, reason)
        if rstatus:
            rstatus.update(ranks, hosts)
        else:
            line = StatusLine(state, ranks, hosts, reason)
            self.bystate[f"{state}:{reason}"] = line
            self.statuslines.append(line)

        self._idset_update(state, ranks)

    @classmethod
    def from_status_response(cls, resp, fmt):

        #  Return empty ResourceStatus object if resp not set:
        #  (mainly used for testing)
        if not resp:
            return cls()

        if isinstance(resp, str):
            resp = json.loads(resp)

        rstat = cls(resp["R"])

        #  Append a line for listing all ranks/hosts
        rstat.append("all", rstat.all)

        #  "online", "offline", "exclude" keys contain idsets
        #    specifying the set of ranks in that state:
        #
        for state in ["online", "offline", "exclude"]:
            rstat.append(state, IDset(resp[state]))

        #  "drain" key contains a dict of idsets with timestamp,reason
        #
        drained = 0
        for ranks, entry in resp["drain"].items():
            #  Only include reason if it will be displayed in format
            reason = ""
            if "reason" in fmt:
                reason = entry["reason"]
            rstat.append("drain", IDset(ranks), reason)
            drained = drained + 1

        #  If no drained nodes, append an empty StatusLine
        if drained == 0:
            rstat.append("drain")

        #  "avail" is computed from above
        rstat.append("avail", rstat.avail)

        return rstat


def status_help(args, valid_states, headings):
    if args.states == "help":
        LOGGER.info("valid states: %s", ",".join(valid_states))
    if args.format == "help":
        LOGGER.info("valid formats: %s", ",".join(headings.keys()))
    sys.exit(0)


def status_get_state_list(args, valid_states, default_states):
    #  Get list of states from command, or a default:
    if args.states:
        states = args.states
    else:
        states = default_states

    #  Warn if listed state is not valid
    states = states.split(",")
    for state in states:
        if state not in valid_states:
            LOGGER.error("Invalid resource state %s specified", state)
            LOGGER.info("valid states: %s", ",".join(valid_states))
            sys.exit(1)

    return states


def status(args):
    valid_states = ["all", "online", "avail", "offline", "exclude", "drain"]
    default_states = "avail,offline,exclude,drain"
    headings = {
        "state": "STATUS",
        "nnodes": "NNODES",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "reason": "REASON",
    }

    #  Emit list of valid states or formats if requested
    if "help" in [args.states, args.format]:
        status_help(args, valid_states, headings)

    #  Get state list from args or defaults:
    states = status_get_state_list(args, valid_states, default_states)

    #  Include reason field only with -vv
    if args.verbose >= 2:
        fmt = "{state:>10} {nnodes:>6} {ranks:<15} {reason:<25} {nodelist}"
    else:
        fmt = "{state:>10} {nnodes:>6} {ranks:<15} {nodelist}"
    if args.format:
        fmt = args.format

    #  Get payload from stdin or from resource.status RPC:
    if args.from_stdin:
        resp = sys.stdin.read()
    else:
        resp = RPC(flux.Flux(), "resource.status", nodeid=0).get()

    rstat = ResourceStatus.from_status_response(resp, fmt)

    formatter = flux.util.OutputFormat(headings, fmt, prepend="0.")
    if not args.no_header:
        print(formatter.header())
    for line in sorted(rstat, key=lambda x: valid_states.index(x.state)):
        if line.state not in states:
            continue
        #  Skip empty lines unless --verbose or --states
        if line.nnodes == 0 and args.states is None and not args.verbose:
            continue
        print(formatter.format(line))


def list_handler(args):
    valid_states = ["up", "down", "allocated", "free", "all"]
    headings = {
        "state": "STATE",
        "nnodes": "NNODES",
        "ncores": "NCORES",
        "ngpus": "NGPUS",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "rlist": "LIST",
    }

    states = args.states.split(",")
    for state in states:
        if state not in valid_states:
            LOGGER.error("Invalid resource state %s specified", state)
            sys.exit(1)

    if args.verbose:
        fmt = "{state:>10} {nnodes:>6} {ncores:>8} {ngpus:>8} {rlist}"
    else:
        fmt = "{state:>10} {nnodes:>6} {ncores:>8} {ngpus:>8} {nodelist}"
    if args.format:
        fmt = args.format

    formatter = flux.util.OutputFormat(headings, fmt, prepend="0.")

    if args.from_stdin:
        resources = SchedResourceList(json.load(sys.stdin))
    else:
        resources = resource_list(flux.Flux()).get()

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
        "-f",
        "--force",
        action="store_true",
        help="Force overwrite of existing drain reason",
    )
    drain_parser.add_argument(
        "-u",
        "--update",
        action="store_true",
        help="Update only. Do not return an error if one or more targets "
        + "are already drained. Do not overwrite any existing drain reason.",
    )
    drain_parser.add_argument(
        "targets", nargs="?", help="List of targets to drain (IDSET or HOSTLIST)"
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

    status_parser = subparsers.add_parser(
        "status", formatter_class=flux.util.help_formatter()
    )
    status_parser.add_argument(
        "-v",
        "--verbose",
        action="count",
        default=0,
        help="Include reason if available",
    )
    status_parser.add_argument(
        "-o",
        "--format",
        help="Specify output format using Python's string format syntax",
    )
    status_parser.add_argument(
        "-s",
        "--states",
        metavar="STATE,...",
        help="Output resources in given states",
    )
    status_parser.add_argument(
        "-n", "--no-header", action="store_true", help="Suppress header output"
    )
    status_parser.add_argument(
        "--from-stdin", action="store_true", help=argparse.SUPPRESS
    )
    status_parser.set_defaults(func=status)

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
