##############################################################
# Copyright 2021 Lawrence Livermore National Security, LLC
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
import concurrent.futures

import flux
import flux.uri
from flux.util import Tree
from flux.job import JobInfo, JobInfoFormat, JobList, JobID

DETAILS_FORMAT = {
    "default": (
        "{id.f58:>12} {username:<8.8} {status_abbrev:>2.2} {ntasks:>6h}"
        " {nnodes:>6h} {runtime!F:>8h}"
    ),
    "resources": (
        "{id.f58:>12} "
        "{instance.resources.all.nnodes:>5} "
        "{instance.resources.allocated.nnodes:>5} "
        "{instance.resources.all.ncores:>5} "
        "{instance.resources.allocated.ncores:>5} "
        "{instance.resources.all.ngpus:>5} "
        "{instance.resources.allocated.ngpus:>5}"
    ),
    "progress": (
        "{id.f58:>12} {ntasks:>6h} {instance.stats.total:>6} "
        "{instance.progress!P:>5} {instance.utilization!P:>5} "
        "{instance.gpu_utilization!P:>5} {runtime!H:>10}"
    ),
    "stats": "{id.f58:>12} {instance.stats:^25} {runtime!H:>10}",
}

LABEL_FORMAT = {
    "default": "{name}",
    "all": "{name}:{status_abbrev}",
}
PARENT_FORMAT = {
    "default": "{name}",
    "all": "{name}",
}

LOGGER = logging.getLogger("flux-pstree")


class TreeFormatter:
    """
    Initialize formatters for Tree labels, parent labels, and optional
    prefix
    """

    def __init__(self, args):

        #  If -o, --label used, then also set parent label format:
        if args.label and not args.parent_label:
            args.parent_label = args.label

        #  If -a, --all, then use "all" specific labels unless labels
        #   were set explicitly:
        if args.all:
            if not args.label:
                args.label = LABEL_FORMAT["all"]
            if not args.parent_label:
                args.parent_label = PARENT_FORMAT["all"]

        #  O/w, set default labels:
        if not args.label:
            args.label = LABEL_FORMAT["default"]
        if not args.parent_label:
            args.parent_label = PARENT_FORMAT["default"]

        #  For -p, --parent-ids, prepend jobid to parent labels:
        if args.parent_ids:
            args.parent_label = f"{{id.f58}} {args.parent_label}"

        self.label = JobInfoFormat(args.label)
        self.parent = JobInfoFormat(args.parent_label)

        #  Set prefix format if -x, --details, or --prefix-format was used
        self.prefix = None
        if args.extended or args.details or args.prefix_format:
            if not args.details:
                args.details = "default"
            if not args.prefix_format:
                try:
                    args.prefix_format = DETAILS_FORMAT[args.details]
                except KeyError:
                    LOGGER.error("Unknown --details format '%s'", args.details)
                    sys.exit(1)
            self.prefix = JobInfoFormat(args.prefix_format)

    def format(self, job, parent):
        """Format provided job label (as parent label if parent == True)"""
        if parent:
            return self.parent.format(job)
        return self.label.format(job)

    def format_prefix(self, job):
        """Format Tree prefix if configured, otherwise return empty string"""
        if self.prefix:
            return self.prefix.format(job)
        return ""


def process_entry(entry, formatter, filters, level, max_level, combine):

    job = JobInfo(entry).get_instance_info()

    # pylint: disable=comparison-with-callable
    parent = job.uri and job.state_single == "R"

    label = formatter.format(job, parent)
    prefix = formatter.format_prefix(job)

    if not parent:
        return Tree(label, prefix)
    return load_tree(
        label,
        formatter,
        prefix=prefix,
        uri=str(job.uri),
        filters=filters,
        level=level + 1,
        max_level=max_level,
        combine_children=combine,
    )


# pylint: disable=too-many-locals
def load_tree(
    label,
    formatter,
    prefix="",
    uri=None,
    filters=None,
    combine_children=True,
    max_level=999,
    level=0,
    skip_root=True,
    jobids=None,
):

    #  Only apply filters below root unless no_skip_root
    orig_filters = filters
    if filters is None or (level == 0 and skip_root):
        filters = ["running"]

    tree = Tree(label, prefix=prefix, combine_children=combine_children)
    if level > max_level:
        return tree

    #  Attempt to load jobs from uri
    #  This may fail if the instance hasn't loaded the job-list module
    #   or if the current user is not owner
    #
    try:
        jobs_rpc = JobList(
            flux.Flux(uri), ids=jobids, filters=filters, attrs=["all"]
        ).fetch_jobs()
        jobs = jobs_rpc.get_jobs()
    except (OSError, FileNotFoundError):
        return tree

    #  Print all errors accumulated in JobList RPC:
    #  fetch_jobs() may not set errors list, must check first
    if hasattr(jobs_rpc, "errors"):
        try:
            for err in jobs_rpc.errors:
                print(err, file=sys.stderr)
        except EnvironmentError:
            pass

    #  Since the executor cannot be used recursively, start one per
    #   loop iteration. This is very wasteful but greatly speeds up
    #   execution with even a moderate number of jobs using the ssh:
    #   connector. At some point this should be replaced by a global
    #   thread pool that can work with recursive execution.
    #
    executor = concurrent.futures.ThreadPoolExecutor()
    futures = []
    for entry in jobs:
        futures.append(
            executor.submit(
                process_entry,
                entry,
                formatter,
                orig_filters,
                level,
                max_level,
                combine_children,
            )
        )

    for future in futures:
        tree.append_tree(future.result())

    return tree


class FilterAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, values)
        setattr(namespace, "filtered", True)


# pylint: disable=redefined-builtin
class FilterTrueAction(argparse.Action):
    def __init__(
        self,
        option_strings,
        dest,
        const=True,
        default=False,
        required=False,
        help=None,
        metavar=None,
    ):
        super(FilterTrueAction, self).__init__(
            option_strings=option_strings,
            dest=dest,
            nargs=0,
            const=const,
            default=default,
            help=help,
        )

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, self.const)
        setattr(namespace, "filtered", True)


class YesNoAction(argparse.Action):
    """Simple argparse.Action for options with yes|no arguments"""

    def __init__(
        self,
        option_strings,
        dest,
        help=None,
        metavar="[yes|no]",
    ):
        super().__init__(
            option_strings=option_strings, dest=dest, help=help, metavar=metavar
        )

    def __call__(self, parser, namespace, value, option_string=None):
        if value not in ["yes", "no"]:
            raise ValueError(f"{option_string} requires either 'yes' or 'no'")
        setattr(namespace, self.dest, value == "yes")


def parse_args():
    parser = argparse.ArgumentParser(
        prog="flux-pstree", formatter_class=flux.util.help_formatter()
    )
    parser.add_argument(
        "-a",
        "--all",
        action=FilterTrueAction,
        help="Include all jobs for current user in all states",
    )
    parser.add_argument(
        "-c",
        "--count",
        action=FilterAction,
        type=int,
        metavar="N",
        default=1000,
        help="Limit to N jobs per instance (default 1000)",
    )
    parser.add_argument(
        "-f",
        "--filter",
        action=FilterAction,
        type=str,
        metavar="STATE|RESULT",
        default="running",
        help="list jobs with specific job state or result",
    )
    parser.add_argument(
        "-x",
        "--extended",
        action="store_true",
        help="print extended details before tree output",
    )
    parser.add_argument(
        "-l",
        "--long",
        action="store_false",
        dest="truncate",
        help="do not truncate long lines to terminal width",
    )
    parser.add_argument(
        "-L",
        "--level",
        type=int,
        metavar="N",
        default=999,
        help="only descend N levels",
    )
    parser.add_argument(
        "-p",
        "--parent-ids",
        action="store_true",
        help="include jobids in parent labels",
    )
    parser.add_argument(
        "-n",
        "--no-header",
        action="store_true",
        help="Suppress header with -x, --extended",
    )
    parser.add_argument(
        "-X",
        "--no-combine",
        action="store_false",
        dest="combine_children",
        help="disable combination of identical children",
    )
    parser.add_argument(
        "-o",
        "--label",
        metavar="FORMAT",
        help="change label format (default='{name}')",
    )
    parser.add_argument(
        "--parent-label",
        metavar="FORMAT",
        help="change label format for parent only",
    )
    parser.add_argument(
        "--detail-format",
        metavar="FORMAT",
        help="specify output format for extended details " + "(implies -x, --extended)",
        dest="prefix_format",
    )
    parser.add_argument(
        "-d",
        "--details",
        metavar="NAME",
        help="Select a named extended details format ("
        + ",".join(DETAILS_FORMAT.keys())
        + ")",
    )
    parser.add_argument(
        "-C",
        "--compact",
        action="store_true",
        help="Use compact tree connectors",
    )
    parser.add_argument(
        "--ascii",
        action="store_true",
        help="Use ASCII character tree connectors",
    )
    parser.add_argument(
        "--skip-root",
        action=YesNoAction,
        help="suppress or include the enclosing instance in output. "
        + "Default is 'no' unless the -x, --extended or -d, --details "
        + "options are used.",
    )
    parser.add_argument(
        "jobids",
        metavar="JOBID",
        type=JobID,
        nargs="*",
        help="Limit output to specific jobids",
    )
    parser.set_defaults(filtered=False)
    return parser.parse_args()


class RootJobID(JobID):
    """Mock JobID class for pstree root (enclosing instance)

    Replaces the encode method of the JobID class to always return ".",
    so that normal `job.id.X` formatting always returns "." to indicate
    root of our tree.
    """

    def __new__(cls):
        return int.__new__(cls, 0)

    # pylint: disable=no-self-use
    # pylint: disable=unused-argument
    def encode(self, encoding="dec"):
        return "."


def get_root_jobinfo():
    """Fetch a mock JobInfo object for the current enclosing instance"""

    handle = flux.Flux()
    size = handle.attr_get("size")

    try:
        #  If the enclosing instance has a jobid and a parent-uri, then
        #   fill in data from job-list in the parent:
        #
        jobid = JobID(handle.attr_get("jobid"))
        parent = flux.Flux(handle.attr_get("parent-uri"))
        info = JobList(parent, ids=[jobid]).fetch_jobs().get_jobs()[0]
    except OSError:
        #  Make a best-effort attempt to create a mock job info dictionary
        uri = handle.attr_get("local-uri")
        nodelist = handle.attr_get("hostlist")
        userid = handle.attr_get("security.owner")
        info = dict(
            id=0,
            userid=int(userid),
            state=flux.constants.FLUX_JOB_STATE_RUN,
            name=".",
            ntasks=int(size),
            nnodes=int(size),
            nodelist=nodelist,
            annotations={"user": {"uri": uri}},
        )
        try:
            info["t_run"] = float(handle.attr_get("broker.starttime"))
        except OSError:
            pass

    #  If 'ranks' idset came from parent, it could be confusing,
    #   rewrite ranks to be relative to current instance, i.e.
    #   0-(size-1)
    #
    info["ranks"] = "0-{}".format(int(size) - 1)

    #  Fetch instance-specific information for the current instance:
    job = JobInfo(info).get_instance_info()

    #  If no jobid was discovered for the root instance, use RootJobID()
    if job.id == 0:
        job.id = RootJobID()

    return job


@flux.util.CLIMain(LOGGER)
def main():

    sys.stdout = open(sys.stdout.fileno(), "w", encoding="utf8")

    args = parse_args()
    if args.jobids and args.filtered:
        LOGGER.warning("filtering options ignored with jobid list")
    if args.ascii and args.compact:
        LOGGER.fatal("Choose only one of --ascii, --compact")

    if args.all:
        args.filter = "pending,running,inactive"

    formatter = TreeFormatter(args)

    #  Default for skip_root is True if there is a "prefix" format or
    #   specific jobids are targetted, possibly overridden by the value of
    #   --skip-root provided by user
    #
    skip_root = formatter.prefix is not None or args.jobids
    if args.skip_root is not None:
        skip_root = args.skip_root

    if skip_root:
        label = "."
        prefix = None
    else:
        root = get_root_jobinfo()
        label = formatter.format(root, True)
        prefix = formatter.format_prefix(root)

    tree = load_tree(
        label,
        formatter,
        prefix=prefix,
        filters=[args.filter],
        max_level=args.level,
        skip_root=skip_root,
        combine_children=args.combine_children,
        jobids=args.jobids,
    )

    if args.compact:
        style = "compact"
    elif args.ascii:
        style = "ascii"
    else:
        style = "box"

    if formatter.prefix and not args.no_header:
        print(formatter.prefix.header())
    tree.render(skip_root=skip_root, style=style, truncate=args.truncate)
