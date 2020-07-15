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
import re
from itertools import count, groupby
import json

import flux
from flux.rpc import RPC
from flux.memoized_property import memoized_property


def drain(args):
    """
    Send a drain request to resource module for args.idset
    """
    RPC(
        flux.Flux(),
        "resource.drain",
        {"idset": args.idset, "reason": " ".join(args.reason)},
    ).get()


def undrain(args):
    """
    Send an "undrain" request to resource module for args.idset
    """
    RPC(flux.Flux(), "resource.undrain", {"idset": args.idset}).get()


def idset_strip(idset):
    return idset.strip("[").rstrip("]")


def idset_range(idset):
    start, end = map(int, idset.split("-"))
    return range(start, end + 1)


def idset_expand(idset):
    result = []
    if not idset:
        return result
    for i in idset_strip(idset).split(","):
        if "-" not in i:
            result.append(int(i))
        else:
            result.extend(idset_range(i))
    return result


def idset_toset(idset):
    result = set()
    if not idset:
        return result
    for i in idset_strip(idset).split(","):
        if "-" not in i:
            result.add(int(i))
        else:
            for j in idset_range(i):
                result.add(j)
    return result


def bracket(setstr):
    """
    Add brackets to idset if not already present and idset is not single number
    """
    if not setstr or setstr[0] == "[" or re.match(r"^[^,-]+$", setstr):
        return setstr
    return f"[{setstr}]"


def idset_encode(idlist):
    """
    Encode list of integers in idlist into an idset string
    """
    ranges = []
    # Collect consecutive integers:
    # See: https://stackoverflow.com/questions/3429510
    for x in (list(x) for _, x in groupby(idlist, lambda x, c=count(): next(c) - x)):
        current = f"{x[0]}"
        if len(x) > 1:
            current += f"-{x[-1]}"
        ranges.append(current)
    return bracket(",".join(ranges))


class Rv1:
    """
    Simple class encapsulating a Flux Rv1 resource set
    """

    def __init__(self, rset=None, state=None):
        # pylint: disable=invalid-name
        self.R_lite = []
        if rset is not None:
            self.R_lite = rset["execution"]["R_lite"]
        if state:
            self._state = state

    def __str__(self):
        """
        Return short-form string for an Rv1 resource set (cores only)
        """
        items = []
        # Group ranks with same core idsets for more compact output
        for _, x in groupby(self.R_lite, lambda x: x["children"]["core"]):
            group = list(x)
            ranks = set()
            cores = bracket(group[0]["children"]["core"])
            for i in (idset_expand(entry["rank"]) for entry in group):
                ranks.update(i)
            if cores:
                items.append("rank{}/core{}".format(idset_encode(ranks), cores))
        return " ".join(items)

    @classmethod
    def from_setlist(cls, setlist):
        """
        Build an Rv1 dictionary from a list-of-sets style resource set,
        then use this to construct an Rv1 object
        """
        rlite = []
        for rank, children in setlist.items():
            entry = {"rank": str(rank), "children": {}}
            for child, idset in children.items():
                entry["children"][child] = idset_encode(idset)
            rlite.append(entry)

        return Rv1({"execution": {"R_lite": rlite}})

    def setlist(self):
        """
        Return a "setlist" representation of a resource set.
        This is a dictionary of ranks, with each entry pointing
        to a dictionary of Python sets, one for each child resource type.
        e.g.
        R = { "0": { "core": { 0, 1, 2, 3 }, "gpu": { 0 } }
        """
        result = {}
        for entry in self.R_lite:
            for rank in idset_expand(entry["rank"]):
                result[rank] = {}
                for child, idset in entry["children"].items():
                    result[rank][child] = idset_toset(idset)
        return result

    def subtract(self, res):
        """
        Subtract one Rv1 resource set from another and return the result
        """
        set1 = self.setlist()
        set2 = res.setlist()
        for rank, children in set2.items():
            for child in children:
                if rank in set1 and child in set1[rank]:
                    set1[rank][child] -= set2[rank][child]
                    if not set1[rank][child]:
                        del set1[rank][child]
                    if not set1[rank]:
                        del set1[rank]
        return Rv1.from_setlist(set1)

    def __sub__(self, res):
        return self.subtract(res)

    def _count_resource(self, name):
        """
        Return total number of child resources of name "name"
        """
        total = 0
        for entry in self.R_lite:
            ranks = len(idset_expand(entry["rank"]))
            try:
                nitems = len(idset_expand(entry["children"][name]))
                total += ranks * nitems
            except KeyError:
                pass
        return total

    @property
    def state(self):
        return self._state

    @state.setter
    def state(self, value):
        self._state = value

    @memoized_property
    def rlist(self):
        return str(self)

    @memoized_property
    def ranks(self):
        ranks = []
        for entry in self.R_lite:
            ranks.extend(idset_expand(entry["rank"]))
        return idset_encode(ranks)

    @memoized_property
    def nnodes(self):
        nnodes = 0
        for entry in self.R_lite:
            nnodes += len(idset_expand(entry["rank"]))
        return nnodes

    @memoized_property
    def ncores(self):
        return self._count_resource("core")

    @memoized_property
    def ngpus(self):
        return self._count_resource("gpu")


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
            setattr(self, f"_{state}", Rv1(resp.get(state), state=state))

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
    drain_parser.add_argument("idset", help="IDSET to drain")
    drain_parser.add_argument("reason", help="Reason", nargs=argparse.REMAINDER)
    drain_parser.set_defaults(func=drain)

    undrain_parser = subparsers.add_parser(
        "undrain", formatter_class=flux.util.help_formatter()
    )
    undrain_parser.add_argument("idset", help="IDSET to resume")
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
    list_parser.set_defaults(func=list_handler)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
