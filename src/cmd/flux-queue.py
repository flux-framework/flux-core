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
import json
import logging
import math
import sys

import flux
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
                "{limits.range.nnodes:>10} "
                "{limits.range.ncores:>10} "
                "{limits.range.ngpus:>10}"
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


class QueueLimitsJobSizeInfo:
    def __init__(self, name, config, minormax):
        self.name = name
        self.config = config
        self.minormax = minormax

    def get_limit(self, key):
        try:
            val = self.config["queues"][self.name]["policy"]["limits"]["job-size"][
                self.minormax
            ][key]
        except KeyError:
            try:
                val = self.config["policy"]["limits"]["job-size"][self.minormax][key]
            except KeyError:
                val = math.inf if self.minormax == "max" else 0
        if val < 0:
            val = math.inf
        return val

    @property
    def nnodes(self):
        return self.get_limit("nnodes")

    @property
    def ncores(self):
        return self.get_limit("ncores")

    @property
    def ngpus(self):
        return self.get_limit("ngpus")


class QueueLimitsRangeInfo:
    def __init__(self, name, min, max):
        self.name = name
        self.min = min
        self.max = max

    @property
    def nnodes(self):
        return f"{self.min.nnodes}-{self.max.nnodes}"

    @property
    def ncores(self):
        return f"{self.min.ncores}-{self.max.ncores}"

    @property
    def ngpus(self):
        return f"{self.min.ngpus}-{self.max.ngpus}"


class QueueLimitsInfo:
    def __init__(self, name, config):
        self.name = name
        self.config = config
        self.min = QueueLimitsJobSizeInfo(name, config, "min")
        self.max = QueueLimitsJobSizeInfo(name, config, "max")
        self.range = QueueLimitsRangeInfo(name, self.min, self.max)

    @property
    def timelimit(self):
        try:
            duration = self.config["queues"][self.name]["policy"]["limits"]["duration"]
        except KeyError:
            try:
                duration = self.config["policy"]["limits"]["duration"]
            except KeyError:
                duration = "inf"
        t = parse_fsd(duration)
        return t


class QueueDefaultsInfo:
    def __init__(self, name, config):
        self.name = name
        self.config = config

    @property
    def timelimit(self):
        try:
            duration = self.config["queues"][self.name]["policy"]["jobspec"][
                "defaults"
            ]["system"]["duration"]
        except KeyError:
            try:
                duration = self.config["policy"]["jobspec"]["defaults"]["system"][
                    "duration"
                ]
            except KeyError:
                duration = "inf"
        t = parse_fsd(duration)
        return t


class QueueInfo:
    def __init__(self, name, config, enabled, started):
        self.name = name
        self.config = config
        self.limits = QueueLimitsInfo(name, config)
        self.defaults = QueueDefaultsInfo(name, config)
        self.scheduling = "started" if started else "stopped"
        self.submission = "enabled" if enabled else "disabled"
        self._enabled = enabled
        self._started = started

    def __getattr__(self, attr):
        try:
            return getattr(self, attr)
        except (KeyError, AttributeError):
            raise AttributeError("invalid QueueInfo attribute '{}'".format(attr))

    @property
    def queue(self):
        return self.name if self.name else ""

    @property
    def queuem(self):
        try:
            defaultq = self.config["policy"]["jobspec"]["defaults"]["system"]["queue"]
        except KeyError:
            defaultq = ""
        q = self.queue + ("*" if defaultq and self.queue == defaultq else "")
        return q

    @property
    def color_enabled(self):
        return "\033[01;32m" if self._enabled else "\033[01;31m"

    @property
    def color_off(self):
        return "\033[0;0m"

    @property
    def enabled(self):
        return AltField("✔", "y") if self._enabled else AltField("✗", "n")

    @property
    def color_started(self):
        return "\033[01;32m" if self._started else "\033[01;31m"

    @property
    def started(self):
        return AltField("✔", "y") if self._started else AltField("✗", "n")


def fetch_all_queue_status(handle, queues=None):
    if handle is None:
        # Return fake payload if handle is not open (e.g. during testing)
        return {"enable": True, "start": True}
    topic = "job-manager.queue-status"
    if queues is None:
        return handle.rpc(topic, {}).get()
    rpcs = {x: handle.rpc(topic, {"name": x}) for x in queues}
    return {x: rpcs[x].get() for x in rpcs}


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
        "limits.min.nnodes": "MINNODES",
        "limits.max.nnodes": "MAXNODES",
        "limits.min.ncores": "MINCORES",
        "limits.max.ncores": "MAXCORES",
        "limits.min.ngpus": "MINGPUS",
        "limits.max.ngpus": "MAXGPUS",
    }
    config = None
    handle = None

    if args.from_stdin:
        config = json.loads(sys.stdin.read())
    else:
        handle = flux.Flux()
        future = handle.rpc("config.get")
        try:
            config = future.get()
        except Exception as e:
            LOGGER.warning("Could not get flux config: " + str(e))

    fmt = FluxQueueConfig("list").load().get_format_string(args.format)
    formatter = flux.util.OutputFormat(fmt, headings=headings)

    #  Build queue_config from args.queue, or config["queue"] if --queue
    #  was unused:
    queue_config = {}
    if args.queue:
        for queue in args.queue:
            try:
                queue_config[queue] = config["queues"][queue]
            except KeyError:
                raise ValueError(f"No such queue: {queue}")
    elif config and "queues" in config:
        queue_config = config["queues"]

    queues = []
    if config and "queues" in config:
        status = fetch_all_queue_status(handle, queue_config.keys())
        for key, value in queue_config.items():
            queues.append(
                QueueInfo(key, config, status[key]["enable"], status[key]["start"])
            )
    else:
        # single anonymous queue
        status = fetch_all_queue_status(handle)
        queues.append(QueueInfo(None, config, status["enable"], status["start"]))

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
    reason = None
    if args.message:
        reason = " ".join(args.message)
    handle = flux.Flux()
    queue_enable(handle, False, args.queue, args.all, reason)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_enable_status)


def check_legacy_all(handle, name, all, quiet):
    if not name and not all and not quiet:
        future = handle.rpc("job-manager.queue-list")
        try:
            qlist = future.get()
        except Exception:
            return all
        if len(qlist["queues"]) > 0:
            print(
                "warning: --queue/--all not specified, assuming --all", file=sys.stderr
            )
        return True
    return all


def queue_start(handle, start, name, all, nocheckpoint=False, reason=None):
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
    args.all = check_legacy_all(handle, args.queue, args.all, args.quiet)
    queue_start(handle, True, args.queue, args.all)
    if not args.quiet:
        queue_status(handle, args.queue, args.verbose, print_start_status)
        if args.verbose:
            alloc_query(handle)


def stop(args):
    reason = " ".join(args.message) if args.message else None
    handle = flux.Flux()
    args.all = check_legacy_all(handle, args.queue, args.all, args.quiet)
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

    status_parser = subparsers.add_parser(
        "status", formatter_class=flux.util.help_formatter()
    )
    status_parser.add_argument(
        "-q",
        "--queue",
        type=str,
        metavar="NAME",
        help="Specify queue to show (default all)",
    )
    status_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display more detail about internal job manager state",
    )
    status_parser.set_defaults(func=status)

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
    list_parser.add_argument(
        "--from-stdin", action="store_true", help=argparse.SUPPRESS
    )
    list_parser.set_defaults(func=list)

    enable_parser = subparsers.add_parser(
        "enable", formatter_class=flux.util.help_formatter()
    )
    enable_parser.add_argument(
        "-q",
        "--queue",
        type=str,
        metavar="NAME",
        help="Specify queue to enable",
    )
    enable_parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help="Force command to apply to all queues if none specified",
    )
    enable_parser.add_argument(
        "--quiet",
        action="store_true",
        help="Display only errors",
    )
    enable_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display more detail about internal job manager state",
    )
    enable_parser.set_defaults(func=enable)

    disable_parser = subparsers.add_parser(
        "disable", formatter_class=flux.util.help_formatter()
    )
    disable_parser.add_argument(
        "-q",
        "--queue",
        type=str,
        metavar="NAME",
        help="Specify queue to disable",
    )
    disable_parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help="Force command to apply to all queues if none specified",
    )
    disable_parser.add_argument(
        "--quiet",
        action="store_true",
        help="Display only errors",
    )
    disable_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display details about all job manager queues",
    )
    disable_parser.add_argument(
        "--nocheckpoint",
        action="store_true",
        help="Do not checkpoint that the queue has been disableped",
    )
    disable_parser.add_argument(
        "message", help="disable reason", nargs=argparse.REMAINDER
    )
    disable_parser.set_defaults(func=disable)

    start_parser = subparsers.add_parser(
        "start", formatter_class=flux.util.help_formatter()
    )
    start_parser.add_argument(
        "-q",
        "--queue",
        type=str,
        metavar="NAME",
        help="Specify queue to start",
    )
    start_parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help="Force command to apply to all queues if none specified",
    )
    start_parser.add_argument(
        "--quiet",
        action="store_true",
        help="Display only errors",
    )
    start_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display more detail about internal job manager state",
    )
    start_parser.set_defaults(func=start)

    stop_parser = subparsers.add_parser(
        "stop", formatter_class=flux.util.help_formatter()
    )
    stop_parser.add_argument(
        "-q",
        "--queue",
        type=str,
        metavar="NAME",
        help="Specify queue to stop",
    )
    stop_parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help="Force command to apply to all queues if none specified",
    )
    stop_parser.add_argument(
        "--quiet",
        action="store_true",
        help="Display only errors",
    )
    stop_parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Display more detail about internal job manager state",
    )
    stop_parser.add_argument(
        "--nocheckpoint",
        action="store_true",
        help="Do not checkpoint that the queue has been stopped",
    )
    stop_parser.add_argument("message", help="stop reason", nargs=argparse.REMAINDER)
    stop_parser.set_defaults(func=stop)

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
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
