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
import os
import select
import sys

import flux
import flux.util
from flux.hostlist import Hostlist
from flux.idset import IDset
from flux.job import JobID, job_list_id
from flux.resource import resource_status

LOGGER = logging.getLogger("flux-hostlist")

sources_description = """
SOURCES may include:
instance     hosts from the broker 'hostlist' attribute.
jobid        hosts assigned to a job
local        hosts assigned to current job if FLUX_JOB_ID is set, otherwise
             returns the 'instance' hostlist
avail[able]  instance hostlist minus those nodes down or drained
stdin, '-'   read a list of hosts on stdin
hosts        literal list of hosts

The default when no SOURCES are supplied is 'stdin', unless the -l, --local
option is used, in which case the default is 'local'.
"""


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-hostlist",
        epilog=sources_description,
        formatter_class=flux.util.help_formatter(raw_description=True),
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "-e",
        "--expand",
        action="store_true",
        help="Expand hostlist using defined output delimiter",
    )
    parser.add_argument(
        "-d",
        "--delimiter",
        type=str,
        metavar="S",
        default=" ",
        help="Set output delimiter for expanded hostlist (default=' ')",
    )
    group.add_argument(
        "-c",
        "--count",
        action="store_true",
        help="Print the total number of hosts",
    )
    parser.add_argument(
        "-n",
        "--nth",
        type=str,
        metavar="[-]IDS",
        help="Output hosts at indices in idset IDS (-IDS to index from end)",
    )
    parser.add_argument(
        "-L",
        "--limit",
        metavar="N",
        type=int,
        help="Output at most N hosts (-N for the last N hosts)",
    )
    parser.add_argument(
        "-S",
        "--sort",
        action="store_true",
        help="Return sorted result",
    )
    parser.add_argument(
        "-x",
        "--exclude",
        metavar="IDS|HOSTS",
        type=Hostlist,
        help="Exclude all occurrences of HOSTS or indices from final result",
    )
    parser.add_argument(
        "-u",
        "--union",
        "--unique",
        action="store_true",
        help="Return only unique hosts in the final hostlist. "
        + "Without other options, this is the same as the union of all "
        + "hostlist args (default mode is append).",
    )
    group2 = parser.add_mutually_exclusive_group()
    group2.add_argument(
        "-i",
        "--intersect",
        action="store_true",
        help="Return the intersection of all hostlists",
    )
    group2.add_argument(
        "-m",
        "--minus",
        action="store_true",
        help="Subtract all hostlist args from first argument",
    )
    group2.add_argument(
        "-X",
        "--xor",
        action="store_true",
        help="Return the symmetric difference of all hostlists",
    )
    parser.add_argument(
        "-f",
        "--fallback",
        action="store_true",
        help="Fallback to treating jobids that are not found as hostnames"
        + " (for hostnames that are also valid jobids e.g. f1, fuzz100, etc)",
    )
    parser.add_argument(
        "-l",
        "--local",
        action="store_true",
        help="Set the default source to 'local' instead of 'stdin'",
    )
    parser.add_argument(
        "-q",
        "--quiet",
        action="store_true",
        help="No output. Exit with nonzero exit status if hostlist is empty",
    )
    parser.add_argument(
        "sources",
        metavar="SOURCES",
        nargs="*",
        help="(optional) One or more hostlist sources",
    )
    return parser.parse_args()


class FluxHandle:
    """Singleton Flux handle"""

    def __new__(cls):
        if not hasattr(cls, "handle"):
            cls.handle = flux.Flux()
        return cls.handle


class HostlistResult:
    """class representing a simple hostlist result"""

    def __init__(self, hosts, **kwargs):
        self.result = Hostlist(hosts)


class InstanceHostlistResult:
    """class representing a hostlist from the instance hostlist attribute"""

    def __init__(self, *args, **kwargs):
        self.result = Hostlist(FluxHandle().attr_get("hostlist"))


class JobHostlistResult:
    """class representing a job hostlist obtained from job-list"""

    def __init__(self, jobid, **kwargs):
        self.arg = jobid
        self.jobid = JobID(jobid)
        self.fallback = kwargs.get("fallback", False)
        self.future = job_list_id(FluxHandle(), self.jobid, attrs=["nodelist"])

    @property
    def result(self):
        try:
            job = self.future.get_jobinfo()
        except OSError as exc:
            if isinstance(exc, FileNotFoundError):
                if self.fallback:
                    # Fall back to treating potential jobid as hostname
                    return Hostlist(self.arg)
                else:
                    raise ValueError(f"job {self.arg} not found") from None
            else:
                raise ValueError(f"job {self.arg}: {exc}") from None
        return Hostlist(job.nodelist)


class AvailableHostlistResult:
    """class representing available hosts in enclosing instance"""

    def __init__(self, *args, **kwargs):
        """Get local hostlist and return only available hosts"""
        # Store local hostlist in self.hl:
        self.hl = LocalHostlistResult().result
        # Send resource status RPCs to get available idset
        self.rpc = resource_status(FluxHandle())

    @property
    def result(self):
        rstatus = self.rpc.get()
        # Restrict returned hostlist to only those available:
        # Note: avail includes down nodes, so subtract those
        avail = rstatus.avail - rstatus.offline
        return self.hl[avail]


class LocalHostlistResult:
    """class representing 'local' hostlist from enclosing instance or job"""

    def __init__(self, *args, **kwargs):
        self.jobid_result = None
        if "FLUX_JOB_ID" in os.environ:
            # This process is running in the context of a job (not initial
            # program) if "FLUX_JOB_ID" is found in current environment.
            # Fetch hostlist via a query to job-list service:
            self._base = JobHostlistResult(os.environ["FLUX_JOB_ID"])
        else:
            # O/w, this is either an initial program or the enclosing instance
            # is the system instance. Fetch the hostlist attribuee either way
            self._base = InstanceHostlistResult()

    @property
    def result(self):
        return self._base.result


class StdinHostlistResult:
    """class representing a hostlist read on stdin"""

    def __init__(self, *args, **kwargs):
        hl = Hostlist()

        # Note: Previous versions of this command defaulted to reading
        # the current enclosing instance or job hostlist, not from stdin.
        # To avoid potential hangs, only wait for stdin for 15s. This should
        # be removed in a future version
        timeout = float(os.environ.get("FLUX_HOSTLIST_STDIN_TIMEOUT", 15.0))
        if not select.select([sys.stdin], [], [], timeout)[0]:
            raise RuntimeError(f"timeout after {timeout}s waiting for stdin")

        for line in sys.stdin.readlines():
            hl.append(line.rstrip())
        self.result = hl


class HostlistResolver:
    """
    Resolve a set of hostlist references in 'sources' into a list of hostlists.
    """

    result_types = {
        "instance": InstanceHostlistResult,
        "local": LocalHostlistResult,
        "stdin": StdinHostlistResult,
        "-": StdinHostlistResult,
        "avail": AvailableHostlistResult,
        "available": AvailableHostlistResult,
    }

    def __init__(self, sources, fallback=False):
        self._results = []
        self.fallback = fallback
        for arg in sources:
            self.append(arg)

    def append(self, arg):
        if arg in self.result_types:
            lookup = self.result_types[arg](arg, fallback=self.fallback)
            self._results.append(lookup)
        else:
            try:
                #  Try argument as a jobid:
                result = JobHostlistResult(arg, fallback=self.fallback)
                self._results.append(result)
            except ValueError:
                try:
                    #  Try argument as a literal Hostlist
                    self._results.append(HostlistResult(arg))
                except (TypeError, OSError, ValueError):
                    raise ValueError(f"Invalid jobid or hostlist {arg}")

    def results(self):
        return [entry.result for entry in self._results]


def intersect(hl1, hl2):
    """Set intersection of Hostlists hl1 and hl2"""
    result = Hostlist()
    for host in hl1:
        if host in hl2:
            result.append(host)
    result.uniq()
    return result


def difference(hl1, hl2):
    """Return hosts in hl1 not in hl2"""
    result = Hostlist()
    for host in hl1:
        if host not in hl2:
            result.append(host)
    return result


def xor(hl1, hl2):
    """Return hosts in hl1 or hl2 but not both"""
    result = difference(hl1, hl2)
    result.append(difference(hl2, hl1))
    result.uniq()
    return result


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    args = parse_args()

    if not args.sources:
        if args.local:
            args.sources = ["local"]
        else:
            args.sources = ["stdin"]

    hostlists = HostlistResolver(args.sources, fallback=args.fallback).results()

    hl = Hostlist()

    if args.intersect:
        hl = hostlists.pop(0)
        for x in hostlists:
            hl = intersect(hl, x)
    elif args.xor:
        hl = hostlists.pop(0)
        for x in hostlists:
            hl = xor(hl, x)
    elif args.minus:
        hl = hostlists.pop(0)
        for x in hostlists:
            hl.delete(x)
    else:
        for x in hostlists:
            hl.append(x)

    if args.exclude:
        # Delete all occurrences of args.exclude
        count = len(hl)
        while hl.delete(args.exclude) > 0:
            pass
        if len(hl) == count:
            # No hosts were deleted, try args.exclude as idset of indices:
            try:
                exclude = IDset(args.exclude)
                hl = Hostlist([hl[i] for i in range(count) if i not in exclude])
            except ValueError:
                # not a valid idset, just pass unaltered hostlist along
                pass

    if args.sort:
        hl.sort()

    if args.union:
        hl.uniq()

    if args.limit:
        if args.limit > 0:
            hl = hl[: args.limit]
        else:
            hl = hl[args.limit :]

    if args.quiet:
        sys.stdout = open(os.devnull, "w")

    if args.nth is not None:
        if args.nth.startswith("-"):
            # Iterate idset in reverse so that resultant hostlist is in
            # the same order as the input hostlist instead of reversed:
            hl = Hostlist([hl[-x] for x in reversed(list(IDset(args.nth[1:])))])
        else:
            hl = hl[IDset(args.nth)]

    if args.count:
        print(f"{hl.count()}")
    elif args.expand:
        # Convert '\n' specified on command line to actual newline char
        if hl:
            print(args.delimiter.replace("\\n", "\n").join(hl))
    else:
        if hl:
            print(hl.encode())

    if args.quiet and not hl:
        sys.exit(1)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
