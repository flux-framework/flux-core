##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import json
import logging
import os.path
import sys

import flux
from flux.future import WaitAllFuture
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource import ResourceSet, SchedResourceList, resource_list
from flux.rpc import RPC
from flux.util import UtilConfig


class FluxResourceConfig(UtilConfig):
    """flux-resource specific user configuration class"""

    builtin_formats = {}
    builtin_formats["status"] = {
        "default": {
            "description": "Default flux-resource status format string",
            "format": "{state:>10} {nnodes:>6} {nodelist}",
        },
        "long": {
            "description": "Long flux-resource status format string",
            "format": "{state:>10} {nnodes:>6} {reason:<30.30+} {nodelist}",
        },
    }
    builtin_formats["drain"] = {
        "default": {
            "description": "Default flux-resource drain format string",
            "format": (
                "{timestamp!d:%FT%T::<20} {state:<8.8} {ranks:<8.8+} "
                "{reason:<30.30+} {nodelist}"
            ),
        },
    }
    builtin_formats["list"] = {
        "default": {
            "description": "Default flux-resource list format string",
            "format": (
                "{state:>10} ?:{properties:<10.10+} {nnodes:>6} "
                "{ncores:>8} {ngpus:>8} {nodelist}"
            ),
        },
    }

    def __init__(self, subcommand):
        initial_dict = {}
        for key, value in self.builtin_formats.items():
            initial_dict[key] = {"formats": value}
        super().__init__(
            name="flux-resource", subcommand=subcommand, initial_dict=initial_dict
        )

    def validate(self, path, conf):
        """Validate a loaded flux-resource config file as dictionary"""

        for key, value in conf.items():
            if key in ["status", "list", "drain"]:
                for key2, val2 in value.items():
                    if key2 == "formats":
                        self.validate_formats(path, val2)
                    else:
                        raise ValueError(f"{path}: invalid key {key}.{key2}")
            else:
                raise ValueError(f"{path}: invalid key {key}")


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
        drain_list(args)
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
        if reason:
            self.reason = reason
        else:
            self.reason = ""
        if timestamp:
            self.timestamp = timestamp
        else:
            self.timestamp = ""

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


def split_draining(drain_ranks, allocated_ranks):
    """
    Given drain_ranks and allocated_ranks, return "drained" vs "draining"
    idsets as (IDset, state) tuples in a list of 1 or 2 elements.
    """
    draining = drain_ranks.intersect(allocated_ranks)
    drained = drain_ranks.difference(draining)
    return [
        (drain_ranks.copy(), "drain"),
        (draining, "draining"),
        (drained, "drained"),
    ]


class ListStatusRPC(WaitAllFuture):
    """Combination sched.resource-status and resource.status RPC

    Inclusion of sched.resource-status response allows drain/draining
    differentiation in resource status and drain command outputs.
    """

    def __init__(self, handle):
        # Initiate RPCs to both resource.status and sched.resource-status:
        children = [RPC(handle, "resource.status", nodeid=0), resource_list(handle)]
        self.rlist = None
        self.rstatus = None
        self.allocated_ranks = None
        super().__init__(children)

    def get_status(self):
        if not self.rstatus:
            self.get()
            self.rstatus = self.children[0].get()
        return self.rstatus

    def get_allocated_ranks(self):
        if not self.allocated_ranks:
            #
            #  If the scheduler is not loaded, do not propagate an error,
            #   just return an empty idset for allocated ranks.
            #
            try:
                self.get()
                self.rlist = self.children[1].get()
                self.allocated_ranks = self.rlist.allocated.ranks
            except EnvironmentError:
                self.allocated_ranks = IDset()
        return self.allocated_ranks


class ResourceStatus:
    """Container for resource.status RPC response"""

    # pylint: disable=too-many-instance-attributes

    def __init__(self, rset=None, rlist=None):
        self.rset = ResourceSet(rset)
        self.rlist = rlist
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

    def find(self, state, reason="", timestamp=""):
        try:
            return self.bystate[f"{state}:{reason}:{timestamp}"]
        except KeyError:
            return None

    def append(self, state, ranks="", reason=None, timestamp=None):
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
            line = StatusLine(state, ranks, hosts, reason, timestamp)
            self.bystate[f"{state}:{reason}:{timestamp}"] = line
            self.statuslines.append(line)

        self._idset_update(state, ranks)

    @classmethod
    def from_status_response(cls, resp, fmt, allocated=None):

        #  Return empty ResourceStatus object if resp not set:
        #  (mainly used for testing)
        if not resp:
            return cls()

        if allocated is None:
            allocated = IDset()

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
        for drain_ranks, entry in resp["drain"].items():
            for ranks, state in split_draining(IDset(drain_ranks), allocated):
                #  Only include reason if it will be displayed in format
                reason, timestamp = "", ""
                if ranks:
                    if "reason" in fmt:
                        reason = entry["reason"]
                    if "timestamp" in fmt:
                        timestamp = entry["timestamp"]

                rstat.append(state, IDset(ranks), reason, timestamp)
                drained = drained + 1

        #  If no drained nodes, append an empty StatusLine
        if drained == 0:
            for state in ["drain", "draining", "drained"]:
                rstat.append(state)

        #  "avail" is computed from above
        rstat.append("avail", rstat.avail)

        return rstat


def status_help(args, valid_states, headings):
    if args.states == "help":
        LOGGER.info("valid states: %s", ",".join(valid_states))
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
    valid_states = [
        "all",
        "online",
        "avail",
        "offline",
        "exclude",
        "drain",
        "draining",
        "drained",
    ]
    default_states = "avail,offline,exclude,draining,drained"

    headings = {
        "state": "STATUS",
        "nnodes": "NNODES",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "reason": "REASON",
        "timestamp": "TIME",
    }

    #  Emit list of valid states if requested
    if args.states == "help":
        status_help(args, valid_states, headings)

    #  Get state list from args or defaults:
    states = status_get_state_list(args, valid_states, default_states)

    fmt = FluxResourceConfig("status").load().get_format_string(args.format)

    #  Get payload from stdin or from resource.status RPC:
    if args.from_stdin:
        resp = sys.stdin.read()
        allocated = IDset()
    else:
        rpc = ListStatusRPC(flux.Flux())
        resp = rpc.get_status()
        allocated = rpc.get_allocated_ranks()

    rstat = ResourceStatus.from_status_response(resp, fmt, allocated)

    formatter = flux.util.OutputFormat(fmt, headings=headings)

    #  Skip empty lines unless --states or ---skip-empty
    skip_empty = args.skip_empty or not args.states

    lines = []
    for line in sorted(rstat, key=lambda x: valid_states.index(x.state)):
        if line.state not in states:
            continue
        if line.nnodes == 0 and skip_empty:
            continue
        lines.append(line)

    formatter.print_items(lines, no_header=args.no_header)


def drain_list(args):
    fmt = FluxResourceConfig("drain").load().get_format_string(args.format)
    args.from_stdin = False
    args.format = fmt
    args.states = "drained,draining"
    args.skip_empty = True
    status(args)


def resources_uniq_lines(resources, states, formatter):
    """
    Generate a set of resource sets that would produce unique lines given
    the ResourceSet formatter argument. Include only the provided states
    """
    #  uniq_fields are the fields on which to combine like results
    uniq_fields = ["state", "properties"]

    #
    #  Create the uniq format by combining all provided uniq fields:
    #   (but only if > 1 field was provided or "state" is in the list of
    #   fields. This allows something like
    #
    #       flux resource -s all -no {properties}
    #
    #   to work as expected)
    #
    uniq_fmt = ""
    if len(formatter.fields) > 1 or "state" in formatter.fields:
        for field in formatter.fields:
            if field in uniq_fields:
                uniq_fmt += "{" + field + "}:"

    fmt = flux.util.OutputFormat(uniq_fmt, headings=formatter.headings)

    #  Create a mapping of resources sets that generate uniq "lines":
    lines = {}
    for state in states:
        if not resources[state].ranks:
            #
            #  If there are no resources in this state, generate an empty
            #   resource set for output purposes. O/w the output for this
            #   state would be suppressed.
            #
            rset = ResourceSet()
            rset.state = state
            key = fmt.format(rset)
            if key not in lines:
                lines[key] = rset
            else:
                lines[key].add(rset)
            continue

        for rank in resources[state].ranks:
            rset = resources[state].copy_ranks(rank)
            key = fmt.format(rset)

            if key not in lines:
                lines[key] = rset
            else:
                lines[key].add(rset)

    return lines


def list_handler(args):
    valid_states = ["up", "down", "allocated", "free", "all"]
    headings = {
        "state": "STATE",
        "properties": "PROPERTIES",
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

    if args.from_stdin:
        resources = SchedResourceList(json.load(sys.stdin))
    else:
        resources = resource_list(flux.Flux()).get()

    fmt = FluxResourceConfig("list").load().get_format_string(args.format)
    formatter = flux.util.OutputFormat(fmt, headings=headings)

    lines = resources_uniq_lines(resources, states, formatter)
    formatter.print_items(lines.values(), no_header=args.no_header)


def info(args):
    """Convenience function that wraps flux-resource list"""
    if not args.states:
        args.states = "all"
    args.no_header = True
    args.format = "{nnodes} Nodes, {ncores} Cores, {ngpus} GPUs"
    list_handler(args)


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
        "-o",
        "--format",
        default="default",
        help="Specify output format using Python's string format syntax "
        + "or a defined format by name "
        + "(only used when no drain targets specified)",
    )
    drain_parser.add_argument(
        "-n", "--no-header", action="store_true", help="Suppress header output"
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
        "-o",
        "--format",
        default="default",
        help="Specify output format using Python's string format syntax "
        + "or a defined format by name (use 'help' to get a list of names)",
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
    status_parser.add_argument(
        "--skip-empty",
        action="store_true",
        help="Skip empty lines of output even with --states",
    )
    status_parser.set_defaults(func=status)

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.add_argument(
        "-o",
        "--format",
        default="default",
        help="Specify output format using Python's string format syntax "
        + "or a defined format by name (use 'help' to get a list of names)",
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

    # flux-resource info:
    info_parser = subparsers.add_parser(
        "info", formatter_class=flux.util.help_formatter()
    )
    info_parser.add_argument(
        "-s",
        "--states",
        metavar="STATE,...",
        help="Show output only for resources in given states",
    )
    info_parser.add_argument(
        "--from-stdin", action="store_true", help=argparse.SUPPRESS
    )
    info_parser.set_defaults(func=info)

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
