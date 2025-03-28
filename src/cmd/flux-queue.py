##############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import argparse
import errno
import logging
import sys

import flux
from flux.queue import QueueList
from flux.util import AltField, FilterActionSetUpdate, UtilConfig, parse_fsd


def print_enable_status(name, status):
    if name:
        print(f"{name}: ", end="")
    if status["enable"]:
        print("Job submission is enabled")
    else:
        disable_reason = status["disable_reason"]
        print(f"Job submission is disabled: {disable_reason}")


def print_start_status(name, status):
    if name:
        print(f"{name}: ", end="")
    if status["start"]:
        print("Scheduling is started")
    else:
        reason = ": " + status["stop_reason"] if "stop_reason" in status else None
        print(f"""Scheduling is stopped{reason if reason else ""}""")


def print_queue_status(name, status):
    print_enable_status(name, status)
    print_start_status(name, status)


def queue_status_get_one(handle, name=None):
    payload = {"name": name} if name else {}
    future = handle.rpc("job-manager.queue-status", payload, nodeid=0)
    try:
        status = future.get()
    except EnvironmentError:
        LOGGER.error("status: {0}".format(future.error_string()))
        sys.exit(1)
    return status


def queue_status_get(handle, queue, verbose):
    statuses = []
    future = handle.rpc("job-manager.queue-list")
    try:
        qlist = future.get()
    except EnvironmentError:
        LOGGER.error("list: {0}".format(future.error_string()))
        sys.exit(1)
    if not queue or verbose:
        if len(qlist["queues"]) > 0:
            for q in qlist["queues"]:
                status = queue_status_get_one(handle, q)
                statuses.append((q, status))
        else:
            status = queue_status_get_one(handle)
            statuses.append((queue, status))
    else:
        status = queue_status_get_one(handle, queue)
        statuses.append((queue, status))
    return statuses


def queue_status(handle, queue, verbose, out_cb):
    statuses = queue_status_get(handle, queue, verbose)
    for name, status in statuses:
        out_cb(name, status)


def alloc_query(handle):
    future = handle.rpc("job-manager.alloc-query")
    try:
        query = future.get()
    except EnvironmentError:
        LOGGER.error("alloc-query: {0}".format(future.error_string()))
        sys.exit(1)
    print("{0} alloc requests queued".format(query["queue_length"]))
    print("{0} alloc requests pending to scheduler".format(query["alloc_pending"]))
    print("{0} running jobs".format(query["running"]))


def status(args):
    handle = flux.Flux()
    queue_status(handle, args.queue, args.verbose, print_queue_status)
    if args.verbose:
        alloc_query(handle)


class FluxQueueConfig(UtilConfig):
    """flux-queue specific user configuration class"""

    builtin_formats = {}
    builtin_formats["list"] = {
        "default": {
            "description": "Default flux-queue list format string",
            "format": (
                "?:{queuem:<8.8} "
                "{color_enabled}{enabled:>2}{color_off} "
                "{color_started}{started:>2}{color_off} "
                "{defaults.timelimit!F:>8} "
                "{limits.timelimit!F:>8} "
                "{limits.range.effective.nnodes:>10} "
                "{limits.range.effective.ncores:>10} "
                "{limits.range.effective.ngpus:>10}"
            ),
        },
        "policy": {
            "description": "flux-queue list with policy limits only",
            "format": (
                "?:{queuem:<8.8} "
                "{color_enabled}{enabled:>2}{color_off} "
                "{color_started}{started:>2}{color_off} "
                "{defaults.timelimit!F:>8} "
                "{limits.timelimit!F:>8} "
                "{limits.range.nnodes:>10} "
                "{limits.range.ncores:>10} "
                "{limits.range.ngpus:>10}"
            ),
        },
        "nodes": {
            "description": "Show queue status plus all/up/allocated/free nodes",
            "format": (
                "?:{queuem:<8.8} "
                "{color_enabled}{enabled:>2}{color_off} "
                "{color_started}{started:>2}{color_off} "
                "{resources.all.nnodes:>6} "
                "{resources.up.nnodes:>10} "
                "{resources.down.nnodes:>10} "
                "{resources.allocated.nnodes:>11} "
                "{resources.free.nnodes:>10}"
            ),
        },
    }

    def __init__(self, subcommand):
        initial_dict = {}
        for key, value in self.builtin_formats.items():
            initial_dict[key] = {"formats": value}
        super().__init__(
            name="flux-queue", subcommand=subcommand, initial_dict=initial_dict
        )

    def validate(self, path, conf):
        """Validate a loaded flux-queue config file as dictionary"""

        for key, value in conf.items():
            if key in ["list"]:
                for key2, val2 in value.items():
                    if key2 == "formats":
                        self.validate_formats(path, val2)
                    else:
                        raise ValueError(f"{path}: invalid key {key}.{key2}")
            else:
                raise ValueError(f"{path}: invalid key {key}")


class QueueLimitsEffectiveRange:
    """
    QueueLimits wrapper which supports a effective range (configured minimum
    to effective maximum minimum of limit or all resources in queue).
    """

    def __init__(self, limits, resources):
        self.__limits = limits
        self.__resources = resources
        self.__effective_max = {}
        for item in ("nnodes", "ncores", "ngpus"):
            self.__effective_max[item] = min(
                getattr(limits.max, item), getattr(resources.all, item)
            )

    @property
    def nnodes(self):
        effective_max = self.__effective_max["nnodes"]
        return f"{self.__limits.min.nnodes}-{effective_max}"

    @property
    def ncores(self):
        effective_max = self.__effective_max["ncores"]
        return f"{self.__limits.min.ncores}-{effective_max}"

    @property
    def ngpus(self):
        effective_max = self.__effective_max["ngpus"]
        return f"{self.__limits.min.ngpus}-{effective_max}"


class QueueLimitsRange:
    """
    QueueLimits wrapper which supports a string range "{min}-{max}"
    """

    def __init__(self, limits, resources):
        self.effective = QueueLimitsEffectiveRange(limits, resources)
        for item in ("nnodes", "ncores", "ngpus"):
            minimum = getattr(limits.min, item)
            maximum = getattr(limits.max, item)
            setattr(self, item, f"{minimum}-{maximum}")


class QueueLimitsWrapper:
    def __init__(self, info):
        self.__limits = info.limits
        self.range = QueueLimitsRange(info.limits, info.resources)

    def __getattr__(self, attr):
        # Forward most attribute lookups to underlying QueueLimits instance
        return getattr(self.__limits, attr)


class QueueInfoWrapper:
    def __init__(self, queue_info):
        self.__qinfo = queue_info
        self.is_started = queue_info.started
        self.is_enabled = queue_info.enabled
        self.limits = QueueLimitsWrapper(queue_info)

    def __getattr__(self, attr):
        try:
            return getattr(self.__qinfo, attr)
        except (KeyError, AttributeError):
            raise AttributeError("invalid QueueInfo attribute '{}'".format(attr))

    @property
    def scheduling(self):
        return "started" if self.is_started else "stopped"

    @property
    def submission(self):
        return "enabled" if self.is_enabled else "disabled"

    @property
    def queue(self):
        return self.name

    @property
    def queuem(self):
        if self.name and self.is_default:
            return f"{self.name}*"
        return self.name

    @property
    def color_enabled(self):
        return "\033[01;32m" if self.is_enabled else "\033[01;31m"

    @property
    def color_off(self):
        return "\033[0;0m"

    @property
    def enabled(self):
        return AltField("✔", "y") if self.is_enabled else AltField("✗", "n")

    @property
    def color_started(self):
        return "\033[01;32m" if self.is_started else "\033[01;31m"

    @property
    def started(self):
        return AltField("✔", "y") if self.is_started else AltField("✗", "n")


def generate_resource_headings():
    headings = {}
    for item in ("nnodes", "ncores", "ngpus"):
        for state in ("all", "free", "allocated", "up", "down"):
            heading = item[1:].upper()
            if state != "all":
                heading += f":{state[:5].upper()}"
            headings[f"resources.{state}.{item}"] = heading
    return headings


def list(args):
    headings = {
        "queue": "QUEUE",
        "queuem": "QUEUE",
        "submission": "SUBMIT",
        "scheduling": "SCHED",
        "enabled": "EN",
        "started": "ST",
        "enabled.ascii": "EN",
        "started.ascii": "ST",
        "color_enabled": "",
        "color_started": "",
        "color_off": "",
        "defaults.timelimit": "TDEFAULT",
        "limits.timelimit": "TLIMIT",
        "limits.range.nnodes": "NNODES",
        "limits.range.ncores": "NCORES",
        "limits.range.ngpus": "NGPUS",
        "limits.range.effective.nnodes": "NNODES",
        "limits.range.effective.ncores": "NCORES",
        "limits.range.effective.ngpus": "NGPUS",
        "limits.min.nnodes": "MINNODES",
        "limits.max.nnodes": "MAXNODES",
        "limits.min.ncores": "MINCORES",
        "limits.max.ncores": "MAXCORES",
        "limits.min.ngpus": "MINGPUS",
        "limits.max.ngpus": "MAXGPUS",
    }
    headings.update(generate_resource_headings())

    fmt = FluxQueueConfig("list").load().get_format_string(args.format)
    formatter = flux.util.OutputFormat(fmt, headings=headings)

    queues = [QueueInfoWrapper(x) for x in QueueList(flux.Flux(), queues=args.queue)]
    formatter.print_items(queues, no_header=args.no_header)


def queue_enable(handle, enable, name, all, reason=None):
    payload = {"enable": enable, "all": all}
    if name:
        payload["name"] = name
    if reason:
        payload["reason"] = reason
    future = handle.rpc("job-manager.queue-enable", payload, nodeid=0)
    try:
        future.get()
    except EnvironmentError:
        LOGGER.error("enable: {0}".format(future.error_string()))
        sys.exit(1)


def enable(args):
    handle = flux.Flux()
    queue_enable(handle, True, args.queue, args.all)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_enable_status)


def disable(args):
    reason = "disabled by administrator"
    if args.message:
        reason = args.message
    handle = flux.Flux()
    queue_enable(handle, False, args.queue, args.all, reason)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_enable_status)


def check_all(handle, name, all_queues):
    if not name and not all_queues:
        qlist = handle.rpc("job-manager.queue-list").get()
        if len(qlist["queues"]) > 0:
            raise ValueError("Named queues are defined. Specify queues or use --all.")


def queue_start(handle, start, name, all, nocheckpoint=False, reason=None):
    check_all(handle, name, all)
    payload = {"start": start, "all": all, "nocheckpoint": nocheckpoint}
    if name:
        payload["name"] = name
    if reason:
        payload["reason"] = reason
    future = handle.rpc("job-manager.queue-start", payload, nodeid=0)
    try:
        future.get()
    except EnvironmentError:
        LOGGER.error("start: {0}".format(future.error_string()))
        sys.exit(1)


def start(args):
    handle = flux.Flux()
    queue_start(handle, True, args.queue, args.all)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_start_status)
        if args.verbose:
            alloc_query(handle)


def stop(args):
    reason = args.message
    handle = flux.Flux()
    queue_start(handle, False, args.queue, args.all, args.nocheckpoint, reason)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_start_status)
        if args.verbose:
            alloc_query(handle)


def drain(args):
    handle = flux.Flux()
    future = handle.rpc("job-manager.drain")
    try:
        future.wait_for(args.timeout)
        future.get()
    except EnvironmentError as e:
        if e.errno == errno.ETIMEDOUT:
            LOGGER.error("drain: timeout")
        else:
            LOGGER.error("drain: {0}".format(future.error_string()))
        sys.exit(1)


def idle(args):
    handle = flux.Flux()
    future = handle.rpc("job-manager.idle")
    try:
        future.wait_for(args.timeout)
        rsp = future.get()
        pending = rsp["pending"]
    except EnvironmentError as e:
        if e.errno == errno.ETIMEDOUT:
            LOGGER.error("idle: timeout")
        else:
            LOGGER.error("idle: {0}".format(future.error_string()))
        sys.exit(1)
    if not args.quiet or pending > 0:
        print(f"{pending} jobs")


class FSDAction(argparse.Action):
    def __call__(self, parser, namespace, values, option_string=None):
        time = parse_fsd(values)
        setattr(namespace, self.dest, time)


LOGGER = logging.getLogger("flux-queue")


def common_parser_create(subparsers, command):
    """
    Create an options parser for one of the flux-queue subcommands that
    take a common set of arguments. (status, enable, disable, start, stop)
    """
    command_parser = subparsers.add_parser(
        command, formatter_class=flux.util.help_formatter()
    )
    command_parser.add_argument(
        "-q",
        "--queue",
        dest="old_queue",
        type=str,
        metavar="NAME",
        help=argparse.SUPPRESS,
    )
    if command in ("enable", "disable", "start", "stop"):
        command_parser.add_argument(
            "-a",
            "--all",
            action="store_true",
            help="Force command to apply to all queues if none specified",
        )
        command_parser.add_argument(
            "--quiet", action="store_true", help="Display only errors"
        )
    command_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display more detail about internal job manager state",
    )
    if command in ("stop", "disable"):
        verb = dict(stop="stopped", disable="disabled")
        command_parser.add_argument(
            "--nocheckpoint",
            action="store_true",
            help=f"Do not checkpoint that the queue has been {verb[command]}",
        )
        command_parser.add_argument(
            "-m",
            "--message",
            type=str,
            metavar="REASON",
            help=f"Add reason that a queue is {verb[command]}",
        )
    command_parser.add_argument(
        "queue", metavar="QUEUE", nargs="?", help=f"Queue to {command}"
    )
    command_parser.set_defaults(func=globals()[command])
    return command_parser


@flux.util.CLIMain(LOGGER)
def main():
    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    parser = argparse.ArgumentParser(prog="flux-queue")
    subparsers = parser.add_subparsers(
        title="subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    list_parser = subparsers.add_parser(
        "list", formatter_class=flux.util.help_formatter()
    )
    list_parser.add_argument(
        "-q",
        "--queue",
        action=FilterActionSetUpdate,
        default=set(),
        metavar="QUEUE,...",
        help="Include only specified queues in output",
    )
    list_parser.add_argument(
        "-o",
        "--format",
        default="default",
        help="Specify output format using Python's string format syntax "
        + "or a defined format by name (use 'help' to get a list of names)",
    )
    list_parser.add_argument(
        "-n", "--no-header", action="store_true", help="Suppress header output"
    )
    list_parser.set_defaults(func=list)

    for command in ("status", "enable", "disable", "start", "stop"):
        common_parser_create(subparsers, command)

    drain_parser = subparsers.add_parser(
        "drain", formatter_class=flux.util.help_formatter()
    )
    drain_parser.add_argument(
        "-t",
        "--timeout",
        action=FSDAction,
        metavar="DURATION",
        default=-1.0,
        help="timeout after DURATION",
    )
    drain_parser.set_defaults(func=drain)

    idle_parser = subparsers.add_parser(
        "idle", formatter_class=flux.util.help_formatter()
    )
    idle_parser.add_argument(
        "-t",
        "--timeout",
        action=FSDAction,
        metavar="DURATION",
        default=-1.0,
        help="timeout after DURATION",
    )
    idle_parser.add_argument(
        "--quiet",
        action="store_true",
        help="Only display pending job count if nonzero",
    )
    idle_parser.set_defaults(func=idle)

    args = parser.parse_args()

    # Handle use of old `-q, --queue` option for commands that now support
    # the specification of queues in free arguments:
    if "old_queue" in args and args.old_queue is not None:
        if args.queue is not None:
            raise ValueError(
                "Conflicting queues specified as "
                + f"--queue={args.old_queue} and {args.queue}"
            )
        args.queue = args.old_queue

    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
