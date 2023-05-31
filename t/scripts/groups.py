###############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

# groups - manipulate broker groups

import argparse
import logging

import flux
from flux.idset import IDset


def barrier_continuation(rpc, fullset):
    """
    Stop the reactor once the group matches fullset.
    """
    resp = rpc.get()
    ids = IDset(resp["members"])
    if ids.equal(fullset):
        rpc.flux_handle.reactor_stop()
    else:
        rpc.reset()


def barrier(args):
    """
    This is functionally a barrier if run with flux exec on all broker ranks.
    If --leave is specified, leave explicitly, otherwise just disconnect.
    """
    h = flux.Flux()
    size = int(h.attr_get("size"))
    fullset = IDset("0-" + str(size - 1))

    entry = h.rpc(
        "groups.get",
        {"name": args.name},
        nodeid=0,
        flags=flux.constants.FLUX_RPC_STREAMING,
    )
    entry.then(barrier_continuation, fullset)

    h.rpc("groups.join", {"name": args.name})
    h.reactor_run()  # run until idset is full

    if args.leave:
        h.rpc("groups.leave", {"name": args.name}).get()


def watch_continuation(rpc):
    resp = rpc.get()
    print(resp["members"])
    rpc.reset()


def watch(args):
    """
    Print each new value of group.  End with Ctrl-C.
    """
    h = flux.Flux()
    rpc = h.rpc(
        "groups.get",
        {"name": args.name},
        nodeid=0,
        flags=flux.constants.FLUX_RPC_STREAMING,
    )
    rpc.then(watch_continuation)
    h.reactor_run()


def waitfor_continuation(rpc, count):
    """
    Stop the reactor once the group has the right number of members.
    """
    resp = rpc.get()
    ids = IDset(resp["members"])
    if ids.count() == count:
        rpc.flux_handle.reactor_stop()
    else:
        rpc.reset()


def waitfor(args):
    """
    Wait for group to have zero (or --count) members.
    """
    h = flux.Flux()
    rpc = h.rpc(
        "groups.get",
        {"name": args.name},
        nodeid=0,
        flags=flux.constants.FLUX_RPC_STREAMING,
    )
    rpc.then(waitfor_continuation, args.count)
    h.reactor_run()


def get(args):
    """
    Get current value of group.
    This only works on rank 0, but for testing that case we have --rank.
    """
    h = flux.Flux()
    resp = h.rpc("groups.get", {"name": args.name}, nodeid=args.rank).get()
    print(resp["members"])


def join(args):
    """
    Join group.
    If --leave is specified, explicitly leave, else just disconnect.
    If --dubjoin, try to join twice for testing (will fail).
    If --dubleave, try to leave twice for testing (will fail).
    """
    h = flux.Flux()

    h.rpc("groups.join", {"name": args.name}).get()
    if args.dubjoin:
        h.rpc("groups.join", {"name": args.name}).get()

    if args.leave:
        h.rpc("groups.leave", {"name": args.name}).get()
        if args.dubleave:
            h.rpc("groups.leave", {"name": args.name}).get()


LOGGER = logging.getLogger("groups")


@flux.util.CLIMain(LOGGER)
def main():
    parser = argparse.ArgumentParser(prog="groups")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # barrier
    barrier_parser = subparsers.add_parser(
        "barrier",
        usage="groups barrier [--leave] name",
        formatter_class=flux.util.help_formatter(),
    )
    barrier_parser.add_argument("--leave", action="store_true")
    barrier_parser.add_argument("name")
    barrier_parser.set_defaults(func=barrier)

    # watch
    watch_parser = subparsers.add_parser(
        "watch",
        usage="groups watch name",
        formatter_class=flux.util.help_formatter(),
    )
    watch_parser.add_argument("name")
    watch_parser.set_defaults(func=watch)

    # waitfor
    waitfor_parser = subparsers.add_parser(
        "waitfor",
        usage="groups waitfor [--count=N] name",
        formatter_class=flux.util.help_formatter(),
    )
    waitfor_parser.add_argument("--count", type=int, default=0)
    waitfor_parser.add_argument("name")
    waitfor_parser.set_defaults(func=waitfor)

    # get
    get_parser = subparsers.add_parser(
        "get",
        usage="groups get [--rank N] name",
        formatter_class=flux.util.help_formatter(),
    )
    get_parser.add_argument("--rank", type=int, default=0)
    get_parser.add_argument("name")
    get_parser.set_defaults(func=get)

    # join
    join_parser = subparsers.add_parser(
        "join",
        usage="groups join [--dubjoin] [--leave] [--dubleave] name",
        formatter_class=flux.util.help_formatter(),
    )
    join_parser.add_argument("--dubjoin", action="store_true")
    join_parser.add_argument("--leave", action="store_true")
    join_parser.add_argument("--dubleave", action="store_true")
    join_parser.add_argument("name")
    join_parser.set_defaults(func=join)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()


# vi: ts=4 sw=4 expandtab
