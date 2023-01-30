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
from flux.util import parse_fsd


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
    print("{0} free requests pending to scheduler".format(query["free_pending"]))
    print("{0} running jobs".format(query["running"]))


def status(args):
    handle = flux.Flux()
    queue_status(handle, args.queue, args.verbose, print_queue_status)
    if args.verbose:
        alloc_query(handle)


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
