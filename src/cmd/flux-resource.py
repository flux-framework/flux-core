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
import errno
import json
import logging
import os.path
import sys

import flux
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.resource import (
    ResourceSet,
    ResourceStatus,
    SchedResourceList,
    resource_list,
    resource_status,
)
from flux.rpc import RPC
from flux.util import Deduplicator, UtilConfig


class FluxResourceConfig(UtilConfig):
    """flux-resource specific user configuration class"""

    builtin_formats = {}
    builtin_formats["status"] = {
        "default": {
            "description": "Default flux-resource status format string",
            "format": "{state:>10} {color_up}{up:>2}{color_off} {nnodes:>6} {nodelist}",
        },
        "long": {
            "description": "Long flux-resource status format string",
            "format": (
                "{state:>10} {color_up}{up:>2}{color_off} "
                "{nnodes:>6} {reason:<30.30+} {nodelist}"
            ),
        },
    }
    builtin_formats["drain"] = {
        "long": {
            "description": "Long flux-resource drain format string",
            "format": (
                "{timestamp!d:%FT%T::<20} {state:<8.8} {ranks:<8.8+} "
                "{reason:<30.30+} {nodelist}"
            ),
        },
        "default": {
            "description": "Default flux-resource drain format string",
            "format": (
                "{timestamp!d:%b%d %R::<12} {state:<8.8} {reason:<30.30+} {nodelist}"
            ),
        },
    }
    builtin_formats["list"] = {
        "default": {
            "description": "Default flux-resource list format string",
            "format": (
                "{state:>10} ?:{queue:<10.10} ?:{propertiesx:<10.10+} {nnodes:>6} "
                "{ncores:>8} ?:{ngpus:>8} {nodelist}"
            ),
        },
        "rlist": {
            "description": "Format including resource list details",
            "format": (
                "{state:>10} ?:{queue:<8.8} ?:{propertiesx:<10.10+} {nnodes:>6} "
                "{ncores:>8} {ngpus:>8} {rlist}"
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
    elif args.force == 1:
        payload["mode"] = "overwrite"
    elif args.force == 2:
        payload["mode"] = "force-overwrite"
    if args.reason:
        payload["reason"] = " ".join(args.reason)
    try:
        RPC(
            flux.Flux(),
            "resource.drain",
            payload,
            nodeid=0,
        ).get()
    except OSError as exc:
        LOGGER.error(exc)
        if exc.errno == errno.EEXIST:
            LOGGER.info("Use --force to overwrite existing drain reason")
        sys.exit(1)


def undrain(args):
    """
    Send an "undrain" request to resource module for args.targets
    """
    RPC(flux.Flux(), "resource.undrain", {"targets": args.targets}, nodeid=0).get()


class AltField:
    """
    Convenient wrapper for fields that have an ascii and non-ascii
    representation. Allows the ascii representation to be selected with
    {field.ascii}.
    """

    def __init__(self, default, ascii):
        self.default = default
        self.ascii = ascii

    def __str__(self):
        return self.default

    def __format__(self, fmt):
        return str(self).__format__(fmt)


class ResourceStatusLine:
    """Information specific to a given flux resource status line"""

    def __init__(self, state, online, ranks, hosts, reason="", timestamp=""):
        self._state = state
        self._online = online
        self.hostlist = Hostlist(hosts)
        self._ranks = IDset(ranks)
        self.reason = reason
        self.timestamp = timestamp

    def update(self, ranks, hosts):
        self._ranks.add(ranks)
        self.hostlist.append(hosts)
        self.hostlist.sort()

    @property
    def statex(self):
        return self._state

    @property
    def state(self):
        state = self._state
        if not self._online:
            state += "*"
        return state

    @property
    def offline(self):
        return not self._online

    @property
    def status(self):
        return "online" if self._online else "offline"

    @property
    def color_up(self):
        return "\033[01;32m" if self._online else "\033[01;31m"

    @property
    def color_off(self):
        return "\033[0;0m"

    @property
    def up(self):
        return AltField("✔", "y") if self._online else AltField("✗", "n")

    @property
    def nodelist(self):
        return str(self.hostlist)

    @property
    def ranks(self):
        return str(self._ranks)

    @property
    def nnodes(self):
        return len(self.hostlist)

    def __repr__(self):
        return f"{self.state} {self.hostlist}"


def status_excluded(online, include_online, include_offline):
    """
    Return true if the current online status is excluded
    """
    included = (online and include_online) or (not online and include_offline)
    return not included


def statuslines(rstatus, states, formatter, include_online=True, include_offline=True):
    """
    Given a ResourceStatus object and OutputFormat formatter,
    return a set of deduplicated ResourceStatusLine objects for
    display.
    """
    result = Deduplicator(
        formatter=formatter,
        except_fields=["nodelist", "ranks", "nnodes"],
        combine=lambda line, arg: line.update(arg.ranks, arg.hostlist),
    )
    states = set(states)
    for state in ["avail", "exclude"]:
        if not states & {state, "all"}:
            continue
        for online in (True, False):
            if status_excluded(online, include_online, include_offline):
                continue
            status_ranks = rstatus["online" if online else "offline"]
            ranks = rstatus[state].intersect(status_ranks)
            result.append(
                ResourceStatusLine(state, online, ranks, rstatus.nodelist[ranks])
            )
    for state in ["draining", "drained"]:
        if not states & {state, "all", "drain"}:
            continue
        ranks = rstatus[state]
        if not ranks:
            if not status_excluded(True, include_online, include_offline):
                # Append one empty line for "draining" and "drained":
                result.append(ResourceStatusLine(state, True, "", ""))
        for rank in ranks:
            online = rank in rstatus.online
            if status_excluded(online, include_online, include_offline):
                continue
            info = rstatus.get_drain_info(rank)
            result.append(
                ResourceStatusLine(
                    state,
                    online,
                    rank,
                    rstatus.nodelist[rank],
                    timestamp=info.timestamp,
                    reason=info.reason,
                )
            )
    return result


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

    #  If only offline and/or online are specified, then append other
    #  default states to states list, o/w status will return nothing
    copy = list(filter(lambda x: x not in ("offline", "online"), states))
    if not copy:
        states.extend(default_states.split(","))
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
    default_states = "avail,exclude,draining,drained"

    headings = {
        "state": "STATE",
        "statex": "STATE",
        "nnodes": "NNODES",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "reason": "REASON",
        "timestamp": "TIME",
        "status": "STATUS",
        "up": "UP",
        "up.ascii": "UP",
        "color_up": "",
        "color_off": "",
    }

    #  Emit list of valid states if requested
    if args.states == "help":
        status_help(args, valid_states, headings)

    #  Get state list from args or defaults:
    states = status_get_state_list(args, valid_states, default_states)

    fmt = FluxResourceConfig("status").load().get_format_string(args.format)

    #  Get payload from stdin or from resource.status RPC:
    if args.from_stdin:
        input_str = sys.stdin.read()
        rstatus = ResourceStatus(json.loads(input_str) if input_str else None)
    else:
        rstatus = resource_status(flux.Flux()).get()

    if args.include:
        try:
            rstatus.filter(include=args.include)
        except (ValueError, TypeError) as exc:
            raise ValueError(f"--include: {exc}") from None

    formatter = flux.util.OutputFormat(fmt, headings=headings)

    # Remove any `{color*}` fields if color is off
    if args.color == "never" or (args.color == "auto" and not sys.stdout.isatty()):
        formatter = formatter.copy(except_fields=["color_up", "color_off"])

    #  Skip empty lines unless --states or --skip-empty
    skip_empty = args.skip_empty or not args.states

    #  Skip empty offline lines unless offline explicily requested
    skip_empty_offline = "offline" not in states

    #  Include both online and offline lines by default, except if
    #  one of those statuses are explicitly requested, then it
    #  becomes exclusive (unless both are present).
    include_online = "online" in states or "offline" not in states
    include_offline = "offline" in states or "online" not in states

    lines = []
    for line in statuslines(
        rstatus,
        states,
        formatter,
        include_online=include_online,
        include_offline=include_offline,
    ):
        if line.nnodes == 0:
            if skip_empty or line.offline and skip_empty_offline:
                continue
        lines.append(line)
    formatter.print_items(lines, no_header=args.no_header)


def drain_list(args):
    fmt = FluxResourceConfig("drain").load().get_format_string(args.format)
    args.from_stdin = False
    args.format = fmt
    args.states = "drain"
    args.skip_empty = True
    status(args)


class ResourceSetExtra(ResourceSet):
    def __init__(self, arg=None, version=1, flux_config=None):
        self.flux_config = flux_config
        if isinstance(arg, ResourceSet):
            super().__init__(arg.encode(), version)
            if arg.state:
                self.state = arg.state
        else:
            super().__init__(arg, version)

    @property
    def propertiesx(self):
        properties = json.loads(self.get_properties())
        queues = self.queue
        if self.queue:
            queues = queues.split(",")
            for q in queues:
                if q in properties:
                    properties.pop(q)
        return ",".join(properties.keys())

    @property
    def queue(self):
        queues = ""
        if self.flux_config and "queues" in self.flux_config:
            properties = json.loads(self.get_properties())
            for key, value in self.flux_config["queues"].items():
                if not self.rlist:
                    continue
                if "requires" not in value or set(value["requires"]).issubset(
                    set(properties)
                ):
                    queues = queues + "," + key if queues else key
        return queues


def resources_uniq_lines(resources, states, formatter, config):
    """
    Generate a set of resource sets that would produce unique lines given
    the ResourceSet formatter argument. Include only the provided states
    """
    #  uniq_fields are the fields on which to combine like results
    uniq_fields = ["state", "properties", "propertiesx", "queue"]

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
            rset = ResourceSetExtra(flux_config=config)
            rset.state = state
            key = fmt.format(rset)
            if key not in lines:
                lines[key] = rset
            else:
                lines[key].add(rset)
            continue

        for rank in resources[state].ranks:
            rset = resources[state].copy_ranks(rank)
            rset = ResourceSetExtra(rset, flux_config=config)
            key = fmt.format(rset)

            if key not in lines:
                lines[key] = rset
            else:
                lines[key].add(rset)

    return lines


def get_resource_list(args):
    """
    Common function for list_handler() and emit_R()
    """
    valid_states = ["up", "down", "allocated", "free", "all"]
    config = None

    args.states = args.states.split(",")
    for state in args.states:
        if state not in valid_states:
            LOGGER.error("Invalid resource state %s specified", state)
            sys.exit(1)

    if args.from_stdin:
        resources = SchedResourceList(json.load(sys.stdin))
    else:
        handle = flux.Flux()
        rpcs = [resource_list(handle), handle.rpc("config.get")]
        resources = rpcs[0].get()
        try:
            config = rpcs[1].get()
        except Exception as e:
            LOGGER.warning("Could not get flux config: " + str(e))

    if args.include:
        try:
            resources.filter(include=args.include)
        except (ValueError, TypeError) as exc:
            raise ValueError(f"--include: {exc}") from None
    return resources, config


def list_handler(args):
    headings = {
        "state": "STATE",
        "queue": "QUEUE",
        "properties": "PROPERTIES",
        "propertiesx": "PROPERTIES",
        "nnodes": "NNODES",
        "ncores": "NCORES",
        "ngpus": "NGPUS",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "rlist": "LIST",
    }
    resources, config = get_resource_list(args)

    fmt = FluxResourceConfig("list").load().get_format_string(args.format)
    formatter = flux.util.OutputFormat(fmt, headings=headings)

    lines = resources_uniq_lines(resources, args.states, formatter, config)
    formatter.print_items(lines.values(), no_header=args.no_header)


def info(args):
    """Convenience function that wraps flux-resource list"""
    if not args.states:
        args.states = "all"
    args.no_header = True
    args.format = "{nnodes} Nodes, {ncores} Cores, {ngpus} GPUs"
    list_handler(args)


def emit_R(args):
    """Emit R in JSON on stdout for requested set of resources"""
    resources, config = get_resource_list(args)

    rset = ResourceSet()
    for state in args.states:
        try:
            rset.add(resources[state])
        except AttributeError:
            raise ValueError(f"unknown state {state}")
    print(rset.encode())


LOGGER = logging.getLogger("flux-resource")


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

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
        action="count",
        default=0,
        help="Force overwrite of existing drain reason. Specify twice "
        + "to also update drain timestamp.",
    )
    drain_parser.add_argument(
        "-u",
        "--update",
        action="store_true",
        help="Update only. Do not return an error if one or more targets "
        + "are already drained. Do not overwrite any existing drain reason.",
    )
    drain_parser.add_argument(
        "-i",
        "--include",
        metavar="TARGETS",
        help="Include only specified targets in output set. TARGETS may be "
        + "provided as an idset or hostlist.",
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
        "-L",
        "--color",
        type=str,
        metavar="WHEN",
        choices=["never", "always", "auto"],
        nargs="?",
        const="always",
        default="auto",
        help="Use color; WHEN can be 'never', 'always', or 'auto' (default)",
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
        "-i",
        "--include",
        metavar="TARGETS",
        help="Include only specified targets in output set. TARGETS may be "
        + "provided as an idset or hostlist.",
    )
    status_parser.add_argument(
        "-n", "--no-header", action="store_true", help="Suppress header output"
    )
    status_parser.add_argument(
        "-L",
        "--color",
        type=str,
        metavar="WHEN",
        choices=["never", "always", "auto"],
        nargs="?",
        const="always",
        default="auto",
        help="Use color; WHEN can be 'never', 'always', or 'auto' (default)",
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
        "-i",
        "--include",
        metavar="TARGETS",
        help="Include only specified targets in output set. TARGETS may be "
        + "provided as an idset or hostlist.",
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
        "-i",
        "--include",
        metavar="TARGETS",
        help="Include only specified targets in output set. TARGETS may be "
        + "provided as an idset or hostlist.",
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

    R_parser = subparsers.add_parser("R", formatter_class=flux.util.help_formatter())
    R_parser.set_defaults(func=emit_R)
    R_parser.add_argument(
        "-s",
        "--states",
        metavar="STATE,...",
        default="all",
        help="Emit R for resources in given states",
    )
    R_parser.add_argument(
        "-i",
        "--include",
        metavar="TARGETS",
        help="Include only specified targets in output set. TARGETS may be "
        + "provided as an idset or hostlist.",
    )
    R_parser.add_argument("--from-stdin", action="store_true", help=argparse.SUPPRESS)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
