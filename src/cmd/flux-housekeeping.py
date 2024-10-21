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
import logging
import sys
import time

import flux
import flux.util
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.job import JobID
from flux.util import Deduplicator, UtilConfig

LOGGER = logging.getLogger("flux-housekeeping")


class FluxHousekeepingConfig(UtilConfig):
    """flux-housekeeping specific configuration"""

    builtin_formats = {
        "default": {
            "description": "Default flux-housekeeping format string",
            "format": (
                "{id.f58:>12} {nnodes:>6} {pending.nnodes:>7} "
                "{runtime!F:>8} {nodelist}"
            ),
        },
        "pending": {
            "description": "flux-housekeeping format string including active nodes only",
            "format": (
                "{id.f58:>12} {pending.nnodes:>7} {runtime!F:>8} {pending.nodelist}"
            ),
        },
    }

    def __init__(self):
        initial_dict = {"formats": dict(self.builtin_formats)}
        super().__init__(name="flux-housekeeping", initial_dict=initial_dict)


class HKFormat(flux.util.OutputFormat):

    headings = {
        "id": "JOBID",
        "id.dec": "JOBID",
        "id.hex": "JOBID",
        "id.f58": "JOBID",
        "id.f58plain": "JOBID",
        "id.emoji": "JOBID",
        "id.kvs": "JOBID",
        "id.words": "JOBID",
        "id.dothex": "JOBID",
        "t_start": "T_START",
        "runtime": "RUNTIME",
        "ranks": "RANKS",
        "nodelist": "NODELIST",
        "nnodes": "NNODES",
        "allocated.ranks": "RANKS",
        "allocated.nodelist": "NODELIST",
        "allocated.nnodes": "NNODES",
        "pending.ranks": "ACTIVE_RANKS",
        "pending.nodelist": "ACTIVE_NODES",
        "pending.nnodes": "#ACTIVE",
    }


class HousekeepingSet:
    """Container for a set of ranks with ranks, nnodes, nodelist properties"""

    def __init__(self, ranks, hostlist):
        self.ranks = IDset(ranks)
        self.hostlist = hostlist

    @property
    def nnodes(self):
        return self.ranks.count()

    @property
    def nodelist(self):
        return self.hostlist[self.ranks]

    def update(self, other):
        self.ranks += other.ranks


class HousekeepingJob:
    def __init__(self, jobid, stats_info, hostlist):
        self.id = JobID(jobid)
        self.t_start = stats_info["t_start"]
        self.runtime = time.time() - self.t_start
        self.pending = HousekeepingSet(stats_info["pending"], hostlist)
        self.allocated = HousekeepingSet(stats_info["allocated"], hostlist)

    @property
    def ranks(self):
        return self.allocated.ranks

    @property
    def nnodes(self):
        return self.allocated.nnodes

    @property
    def nodelist(self):
        return self.allocated.nodelist

    def filter(self, include_ranks):
        """Filter this HousekeepingJob's ranks to only include include_ranks"""
        self.pending.ranks = self.pending.ranks.intersect(include_ranks)
        self.allocated.ranks = self.allocated.ranks.intersect(include_ranks)

    def combine(self, other):
        self.pending.update(other.pending)
        self.allocated.update(other.allocated)


def housekeeping_list(args):
    handle = flux.Flux()

    hostlist = Hostlist(handle.attr_get("hostlist"))
    stats = handle.rpc("job-manager.stats-get", {}).get()

    include_ranks = None
    if args.include:
        try:
            include_ranks = IDset(args.include)
        except ValueError:
            include_ranks = IDset(hostlist.index(args.include))

    fmt = FluxHousekeepingConfig().load().get_format_string(args.format)
    try:
        formatter = HKFormat(fmt)
    except ValueError as err:
        raise ValueError(f"Error in user format: {err}")

    jobs = Deduplicator(
        formatter=formatter,
        except_fields=[
            "nodelist",
            "ranks",
            "nnodes",
            "pending.nodelist",
            "pending.ranks",
            "pending.nnodes",
        ],
        combine=lambda job, other: job.combine(other),
    )
    for jobid, info in stats["housekeeping"]["running"].items():
        job = HousekeepingJob(jobid, info, hostlist)
        if include_ranks:
            job.filter(include_ranks)
        if job.nnodes > 0:
            jobs.append(job)

    formatter.print_items(jobs, no_header=args.no_header)


def housekeeping_kill(args):
    handle = flux.Flux()
    payload = {"signum": args.signal}

    # Require one selection option (do not default to --all)
    if args.jobid is None and args.targets is None and not args.all:
        raise ValueError("specify at least one of --targets, --jobid, or --all")
    if args.all and args.jobid is not None:
        raise ValueError("do not specify --jobid with --all")
    if args.jobid:
        payload["jobid"] = args.jobid
    if args.targets:
        try:
            ranks = IDset(args.targets)
        except ValueError:
            try:
                hosts = Hostlist(args.targets)
            except ValueError:
                raise ValueError("--targets must be a valid Idset or Hostlist")
            hostlist = Hostlist(handle.attr_get("hostlist"))
            ranks = IDset()
            for host in hosts:
                try:
                    ranks.set(hostlist.find(host))
                except OSError:
                    raise ValueError(f"didn't find {host} in instance hostlist")
        payload["ranks"] = str(ranks)
    flux.Flux().rpc("job-manager.housekeeping-kill", payload).get()


def parse_args():
    parser = argparse.ArgumentParser(prog="flux-housekeeping")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.add_argument(
        "-i",
        "--include",
        metavar="HOSTS|RANKS",
        type=str,
        help="Limit output to housekeeping jobs on HOSTS|RANKS",
    )
    list_parser.add_argument(
        "-n",
        "--no-header",
        action="store_true",
        help="Suppress printing of header line",
    )
    list_parser.add_argument(
        "-o",
        "--format",
        type=str,
        default="default",
        metavar="FORMAT",
        help="Specify output format using Python's string format syntax "
        + " or a defined format by name (use 'help' to get a list of names)",
    )
    list_parser.set_defaults(func=housekeeping_list)

    kill_parser = subparsers.add_parser(
        "kill", formatter_class=flux.util.help_formatter()
    )
    kill_parser.add_argument(
        "-s",
        "--signal",
        metavar="SIGNUM",
        type=int,
        default=15,
        help="Specify signal number to send to housekeeping task",
    )
    kill_parser.add_argument(
        "-t",
        "--targets",
        metavar="RANKS|HOSTS",
        type=str,
        help="Only target specific ranks or hostnames",
    )
    kill_parser.add_argument(
        "-j",
        "--jobid",
        type=JobID,
        help='target housekeeping tasks for this jobid or "all" for all jobs',
    )
    kill_parser.add_argument(
        "--all", action="store_true", help="kill all active housekeeping tasks"
    )
    kill_parser.set_defaults(func=housekeeping_kill)

    return parser.parse_args()


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    args = parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
