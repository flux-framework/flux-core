#############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
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
import os
import sys
from collections import deque

import flux
import flux.kvs
from flux.cli.base import list_split
from flux.conf_builtin import conf_builtin_get
from flux.modprobe import Modprobe
from flux.util import CLIMain, Tree, help_formatter


def disable_modules(M, modules=None):
    disable_list = []
    try:
        disable_list.append(os.environ["FLUX_MODPROBE_DISABLE"])
    except KeyError:
        # ignore if not set
        pass
    if modules is not None:
        disable_list.extend(modules)
    for name in list_split(disable_list):
        M.disable(name)


def set_alternatives(M, alternatives):
    for entry in alternatives:
        name, *rest = entry.split("=", 1)
        if not name or len(rest) != 1:
            raise ValueError(f"--set-alternative: invalid value '{entry}'")
        M.set_alternative(name, rest[0])


def runtasks(args, rcfile, timing=False):
    M = Modprobe(
        confdir=args.confdir, timing=timing, verbose=args.verbose, dry_run=args.dry_run
    )
    t0 = M.timestamp
    M.configure_modules()
    M.read_rcfile(rcfile)
    disable_modules(M)
    M.add_timing("configure", t0)
    deps = M.get_deps(M.solve(M.active_tasks))
    if args.show_deps:
        print(json.dumps(deps))
        sys.exit(0)
    M.run(deps)
    if timing and M.rank == 0:
        flux.kvs.put(M.context.handle, "modprobe.stats", M.timing)
        flux.kvs.commit(M.context.handle)
    sys.exit(M.exitcode)


def run(args):
    runtasks(args, rcfile=args.file)


def rc1(args):
    if os.environ.get("FLUX_RC_USE_MODPROBE") is None:
        # Backwards compat: run rc1.old unless FLUX_RC_USE_MODPROBE is set
        confdir = conf_builtin_get("confdir")
        os.execv(f"{confdir}/rc1.old", ["rc1"])
        sys.exit(1)

    runtasks(args, rcfile="rc1", timing=args.timing)


def rc3(args):
    if os.environ.get("FLUX_RC_USE_MODPROBE") is None:
        # Backwards compat: run rc3.old unless FLUX_RC_USE_MODPROBE is set
        confdir = conf_builtin_get("confdir")
        os.execv(f"{confdir}/rc3.old", ["rc3"])
        sys.exit(1)

    M = Modprobe(confdir=args.confdir, verbose=args.verbose, dry_run=args.dry_run)
    M.configure_modules()
    # rc3 always removes all modules
    M.set_remove()
    M.read_rcfile("rc3")
    deps = M.get_deps(M.active_tasks)
    M.run(deps)
    sys.exit(M.exitcode)


def load(args):
    M = Modprobe(dry_run=args.dry_run).configure_modules()
    try:
        M.load(args.modules)
    except FileExistsError:
        LOGGER.info("All modules and dependencies are already loaded")

    sys.exit(M.exitcode)


def show(args):
    M = Modprobe().configure_modules(args.path)
    set_alternatives(M, args.set_alternative)
    disable_modules(M, args.disable)
    print(json.dumps(M.get_task(args.module).to_dict(), default=str))


def remove(args):
    M = Modprobe(dry_run=args.dry_run).configure_modules()
    M.remove(args.modules)
    sys.exit(M.exitcode)


def list_dependencies(args):
    M = Modprobe().configure_modules()
    set_alternatives(M, args.set_alternative)
    disable_modules(M, args.disable)

    queue = deque()
    visited = set()

    parent = Tree(args.module)
    queue.append(parent)

    while queue:
        tree = queue.popleft()
        task = M.get_task(tree.label)
        if not args.full:
            visited.add(task.name)

        for dep in task.requires:
            if dep not in visited:
                child = Tree(dep)
                queue.append(child)
                tree.append_tree(child)
                if not args.full:
                    visited.add(dep)

        if task.name != tree.label:
            tree.label += f" ({task.name})"

    parent.render()


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-modprobe",
        description="Task and module load/unload management for Flux",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    load_parser = subparsers.add_parser("load", formatter_class=help_formatter())
    load_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't do anything. Print what would be run",
    )
    load_parser.add_argument(
        "modules",
        metavar="MODULE",
        nargs="+",
        help="Module to load",
    )
    load_parser.set_defaults(func=load)

    remove_parser = subparsers.add_parser("remove", formatter_class=help_formatter())
    remove_parser.add_argument(
        "modules",
        metavar="MODULE",
        nargs="+",
        help="Module to remove",
    )
    remove_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't do anything. Print what would be run",
    )
    remove_parser.set_defaults(func=remove)

    run_parser = subparsers.add_parser(
        "run",
        formatter_class=help_formatter(),
    )
    run_parser.add_argument(
        "--show-deps",
        action="store_true",
        help="Display dictionary of tasks to predecessor list and exit",
    )
    run_parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print tasks as they are executed"
    )
    run_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't do anything. Print what would be run",
    )
    run_parser.add_argument(
        "--confdir",
        metavar="DIR",
        default=None,
        help="Set default config directory",
    )
    run_parser.add_argument(
        "file", metavar="FILE", help="Run commands defined in Python rc file FILE"
    )
    run_parser.set_defaults(func=run)

    rc1_parser = subparsers.add_parser("rc1", formatter_class=help_formatter())
    rc1_parser.add_argument(
        "--confdir",
        metavar="DIR",
        default=None,
        help="Set default config directory",
    )
    rc1_parser.add_argument(
        "--timing",
        action="store_true",
        help="Store timing data in KVS in modprobe.stats key",
    )
    rc1_parser.add_argument(
        "--show-deps",
        action="store_true",
        help="Display dictionary of tasks to predecessor list and exit",
    )
    rc1_parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print tasks as they are executed"
    )
    rc1_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't do anything. Print what would be run",
    )
    rc1_parser.set_defaults(func=rc1)

    rc3_parser = subparsers.add_parser("rc3", formatter_class=help_formatter())
    rc3_parser.add_argument(
        "--confdir",
        metavar="DIR",
        default=None,
        help="Set default config directory",
    )
    rc3_parser.add_argument(
        "-v", "--verbose", action="store_true", help="Print tasks as they are executed"
    )
    rc3_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Don't do anything. Print what would be run",
    )
    rc3_parser.set_defaults(func=rc3)

    list_deps_parser = subparsers.add_parser(
        "list-dependencies", formatter_class=help_formatter()
    )
    list_deps_parser.add_argument(
        "-S",
        "--set-alternative",
        metavar="NAME=MODULE",
        action="append",
        default=[],
        help="Set alternative for service NAME to module MODULE",
    )
    list_deps_parser.add_argument(
        "-D",
        "--disable",
        metavar="NAME",
        action="append",
        default=[],
        help="Disable module or task NAME",
    )
    list_deps_parser.add_argument(
        "-f",
        "--full",
        action="store_true",
        help="Full output. Will show duplicates",
    )
    list_deps_parser.add_argument(
        "module",
        metavar="MODULE",
        type=str,
        help="Module name for which to display dependencies",
    )
    list_deps_parser.set_defaults(func=list_dependencies)

    show_parser = subparsers.add_parser("show", formatter_class=help_formatter())
    show_parser.add_argument(
        "--path",
        metavar="PATH",
        type=str,
        help="Set path to module configuration TOML file",
    )
    show_parser.add_argument(
        "-S",
        "--set-alternative",
        metavar="NAME=MODULE",
        action="append",
        default=[],
        help="Set alternative for service NAME to module MODULE",
    )
    show_parser.add_argument(
        "-D",
        "--disable",
        metavar="NAME",
        action="append",
        default=[],
        help="Disable module or task NAME",
    )
    show_parser.add_argument(
        "module",
        metavar="MODULE",
        type=str,
        help="Module name to show",
    )
    show_parser.set_defaults(func=show)

    return parser.parse_args()


LOGGER = logging.getLogger("flux-modprobe")


@CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
