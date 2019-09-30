#!/usr/bin/env flux-python

##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

from __future__ import print_function

import re
import os
import sys
import math
import logging
import argparse
import json
import collections

try:
    collectionsAbc = collections.abc
except AttributeError:
    collectionsAbc = collections

import six
import flux
from flux import job
from flux import util
from datetime import timedelta


class JobSpec:
    def __init__(
        self, command, num_tasks=1, cores_per_task=1, gpus_per_task=None, num_nodes=None
    ):
        """
        Constructor builds the minimum legal v1 jobspec.
        Use setters to assign additional properties.
        """
        if not isinstance(command, (list, tuple)) or not command:
            raise ValueError("command must be a non-empty list or tuple")
        if not isinstance(num_tasks, int) or num_tasks < 1:
            raise ValueError("task count must be a integer >= 1")
        if not isinstance(cores_per_task, int) or cores_per_task < 1:
            raise ValueError("cores per task must be an integer >= 1")
        if gpus_per_task is not None:
            if not isinstance(gpus_per_task, int) or gpus_per_task < 1:
                raise ValueError("gpus per task must be an integer >= 1")
        if num_nodes is not None:
            if not isinstance(num_nodes, int) or num_nodes < 1:
                raise ValueError("node count must be an integer >= 1 (if set)")
            if num_nodes > num_tasks:
                raise ValueError("node count must not be greater than task count")
        children = [self.__create_resource("core", cores_per_task)]
        if gpus_per_task is not None:
            children.append(self.__create_resource("gpu", gpus_per_task))
        if num_nodes is not None:
            num_slots = int(math.ceil(num_tasks / float(num_nodes)))
            if num_tasks % num_nodes != 0:
                # N.B. uneven distribution results in wasted task slots
                task_count_dict = {"total": num_tasks}
            else:
                task_count_dict = {"per_slot": 1}
            slot = self.__create_slot("task", num_slots, children)
            resource_section = self.__create_resource("node", num_nodes, [slot])
        else:
            task_count_dict = {"per_slot": 1}
            slot = self.__create_slot("task", num_tasks, children)
            resource_section = slot

        self.jobspec = {
            "version": 1,
            "resources": [resource_section],
            "tasks": [{"command": command, "slot": "task", "count": task_count_dict}],
            "attributes": {"system": {"duration": 0}},
        }

    def __create_resource(self, res_type, count, with_child=[]):
        assert isinstance(
            with_child, collectionsAbc.Sequence
        ), "child resource must be a sequence"
        assert not isinstance(
            with_child, six.string_types
        ), "child resource must not be a string"
        assert count > 0, "resource count must be > 0"

        res = {"type": res_type, "count": count}

        if len(with_child) > 0:
            res["with"] = with_child
        return res

    def __create_slot(self, label, count, with_child):
        slot = self.__create_resource("slot", count, with_child)
        slot["label"] = label
        return slot

    def __parse_fsd(self, s):
        m = re.match(r".*([smhd])$", s)
        try:
            n = float(s[:-1] if m else s)
        except:
            raise ValueError("invalid Flux standard duration")
        unit = m.group(1) if m else "s"

        if unit == "m":
            seconds = timedelta(minutes=n).total_seconds()
        elif unit == "h":
            seconds = timedelta(hours=n).total_seconds()
        elif unit == "d":
            seconds = timedelta(days=n).total_seconds()
        else:
            seconds = n
        if seconds < 0 or math.isnan(seconds) or math.isinf(seconds):
            raise ValueError("invalid Flux standard duration")
        return seconds

    def set_duration(self, duration):
        """
        Assign a time limit to the job.  The duration may be:
        - a float in seconds
        - a string in Flux Standard Duration
        A duration of zero is interpreted as "not set".
        """
        if isinstance(duration, six.string_types):
            t = self.__parse_fsd(duration)
        elif isinstance(duration, float):
            t = duration
        else:
            raise ValueError("duration must be a float or string")
        if t < 0:
            raise ValueError("duration must not be negative")
        if math.isnan(t) or math.isinf(t):
            raise ValueError("duration must be a normal, finite value")
        self.jobspec["attributes"]["system"]["duration"] = t

    def set_cwd(self, cwd):
        """
        Set working directory of job.
        """
        if not isinstance(cwd, six.string_types):
            raise ValueError("cwd must be a string")
        self.jobspec["attributes"]["system"]["cwd"] = cwd

    def set_environment(self, environ):
        """
        Set (entire) environment of job.
        """
        if not isinstance(environ, collectionsAbc.Mapping):
            raise ValueError("environment must be a mapping")
        self.jobspec["attributes"]["system"]["environment"] = environ

    def __set_treedict(self, d, key, val):
        """
        __set_treedict(d, "a.b.c", 42) is like d[a][b][c] = 42
        but levels are created on demand.
        """
        path = key.split(".", 1)
        if len(path) == 2:
            self.__set_treedict(d.setdefault(path[0], {}), path[1], val)
        else:
            d[key] = val

    def setattr(self, key, val):
        """
        set job attribute
        """
        self.__set_treedict(self.jobspec, "attributes." + key, val)

    def setattr_shopt(self, key, val):
        """
        set job attribute: shell option
        """
        self.setattr("system.shell.options." + key, val)

    def dumps(self):
        return json.dumps(self.jobspec)


class SubmitCmd:
    """
    SubmitCmd submits a job, displays the jobid on stdout, and returns.

    Usage: flux mini submit [OPTIONS] cmd ...
    """

    def __init__(self):
        self.parser = self.create_parser()

    def create_parser(self):
        """
        Create parser with args for submit subcommand
        """
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument(
            "-N", "--nodes", type=int, metavar="N", help="Number of nodes to allocate"
        )
        parser.add_argument(
            "-n",
            "--ntasks",
            type=int,
            metavar="N",
            default=1,
            help="Number of tasks to start",
        )
        parser.add_argument(
            "-c",
            "--cores-per-task",
            type=int,
            metavar="N",
            default=1,
            help="Number of cores to allocate per task",
        )
        parser.add_argument(
            "-g",
            "--gpus-per-task",
            type=int,
            metavar="N",
            help="Number of GPUs to allocate per task",
        )
        parser.add_argument(
            "-t",
            "--time-limit",
            type=str,
            metavar="FSD",
            help="Time limit in Flux standard duration, e.g. 2d, 1.5h",
        )
        parser.add_argument(
            "--priority",
            help="Set job priority (0-31, default=16)",
            type=int,
            metavar="N",
            default=16,
        )
        parser.add_argument(
            "-o",
            "--setopt",
            action="append",
            help="Set shell option (multiple use OK)",
            metavar="KEY=VAL",
        )
        parser.add_argument(
            "--setattr",
            action="append",
            help="Set job attribute (multiple use OK)",
            metavar="KEY=VAL",
        )
        parser.add_argument(
            "--input",
            type=str,
            help="Redirect job stdin from FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--output",
            type=str,
            help="Redirect job stdout to FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--error",
            type=str,
            help="Redirect job stderr to FILENAME, bypassing KVS",
            metavar="FILENAME",
        )
        parser.add_argument(
            "--label-io",
            action="store_true",
            help="Add rank labels to stdout, stderr lines",
        )
        parser.add_argument("--debug", action="store_true", help="Set job debug flag")
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Don't actually submit job, just emit jobspec",
        )
        parser.add_argument(
            "command", nargs=argparse.REMAINDER, help="Job command and arguments"
        )
        return parser

    def submit(self, args):
        """
        Submit job, constructing jobspec from args.
        Returns jobid.
        """
        if not args.command:
            raise ValueError("job command and arguments are missing")

        jobspec = JobSpec(
            args.command,
            num_tasks=args.ntasks,
            cores_per_task=args.cores_per_task,
            gpus_per_task=args.gpus_per_task,
            num_nodes=args.nodes,
        )
        jobspec.set_cwd(os.getcwd())
        jobspec.set_environment(dict(os.environ))
        if args.time_limit is not None:
            jobspec.set_duration(args.time_limit)

        if args.input is not None:
            jobspec.setattr_shopt("input.stdin.type", "file")
            jobspec.setattr_shopt("input.stdin.path", args.input)

        if args.output is not None:
            jobspec.setattr_shopt("output.stdout.type", "file")
            jobspec.setattr_shopt("output.stdout.path", args.output)
            if args.label_io:
                jobspec.setattr_shopt("output.stdout.label", True)

        if args.error is not None:
            jobspec.setattr_shopt("output.stderr.type", "file")
            jobspec.setattr_shopt("output.stderr.path", args.error)
            if args.label_io:
                jobspec.setattr_shopt("output.stderr.label", True)

        if args.setopt is not None:
            for kv in args.setopt:
                tmp = kv.split("=", 1)
                key = tmp[0]
                try:
                    val = json.loads(tmp[1])
                except:
                    val = tmp[1]
                jobspec.setattr_shopt(key, val)

        if args.setattr is not None:
            for kv in args.setattr:
                tmp = kv.split("=", 1)
                key = tmp[0]
                try:
                    val = json.loads(tmp[1])
                except:
                    val = tmp[1]
                jobspec.setattr(key, val)

        if args.dry_run:
            print(jobspec.dumps(), file=sys.stdout)
            sys.exit(0)

        h = flux.Flux()
        flags = 0
        if args.debug:
            flags = flux.constants.FLUX_JOB_DEBUG
        return job.submit(h, jobspec.dumps(), priority=args.priority, flags=flags)

    def main(self, args):
        jobid = self.submit(args)
        print(jobid, file=sys.stdout)

    def get_parser(self):
        return self.parser


class RunCmd(SubmitCmd):
    """
    RunCmd is identical to SubmitCmd, except it attaches the the job
    after submission.  Some additional options are added to modify the
    attach behavior.

    Usage: flux mini run [OPTIONS] cmd ...
    """

    def __init__(self):
        self.parser = self.create_parser()
        self.parser.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=0,
            help="Increase verbosity on stderr (multiple use OK)",
        )

    def main(self, args):
        jobid = self.submit(args)

        # Display job id on stderr if -v
        # N.B. we must flush sys.stderr due to the fact that it is buffered
        # when it points to a file, and os.execvp leaves it unflushed
        if args.verbose > 0:
            print("jobid:", jobid, file=sys.stderr)
            sys.stderr.flush()

        # Build args for flux job attach
        attach_args = ["flux-job", "attach"]
        if args.label_io:
            attach_args.append("--label-io")
        if args.verbose > 1:
            attach_args.append("--show-events")
        if args.verbose > 2:
            attach_args.append("--show-exec")
        attach_args.append(str(jobid))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"] + ":" + os.environ["PATH"]
        os.execvp("flux-job", attach_args)


logger = logging.getLogger("flux-mini")


@util.CLIMain(logger)
def main():
    parser = argparse.ArgumentParser(prog="flux-mini")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # run
    run = RunCmd()
    mini_run_parser_sub = subparsers.add_parser(
        "run", parents=[run.get_parser()], help="run a job interactively"
    )
    mini_run_parser_sub.set_defaults(func=run.main)

    # submit
    submit = SubmitCmd()
    mini_submit_parser_sub = subparsers.add_parser(
        "submit", parents=[submit.get_parser()], help="enqueue a job"
    )
    mini_submit_parser_sub.set_defaults(func=submit.main)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
