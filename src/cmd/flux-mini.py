##############################################################
# Copyright 2019 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

# pylint: disable=duplicate-code
import os
import sys
import logging
import argparse
import json
import fnmatch
import re
from itertools import chain
from string import Template
from collections import ChainMap

import flux
from flux import job
from flux.job import JobspecV1, JobID
from flux import util
from flux import debugged


def filter_dict(env, pattern, reverseMatch=True):
    """
    Filter out all keys that match "pattern" from dict 'env'

    Pattern is assumed to be a shell glob(7) pattern, unless it begins
    with '/', in which case the pattern is a regex.
    """
    if pattern.startswith("/"):
        pattern = pattern[1::].rstrip("/")
    else:
        pattern = fnmatch.translate(pattern)
    regex = re.compile(pattern)
    if reverseMatch:
        return dict(filter(lambda x: not regex.match(x[0]), env.items()))
    return dict(filter(lambda x: regex.match(x[0]), env.items()))


def get_filtered_environment(rules, environ=None):
    """
    Filter environment dictionary 'environ' given a list of rules.
    Each rule can filter, set, or modify the existing environment.
    """
    if environ is None:
        environ = dict(os.environ)
    if rules is None:
        return environ
    for rule in rules:
        #
        #  If rule starts with '-' then the rest of the rule is a pattern
        #   which filters matching environment variables from the
        #   generated environment.
        #
        if rule.startswith("-"):
            environ = filter_dict(environ, rule[1::])
        #
        #  If rule starts with '^', then the result of the rule is a filename
        #   from which to read more rules.
        #
        elif rule.startswith("^"):
            filename = os.path.expanduser(rule[1::])
            with open(filename) as envfile:
                lines = [line.strip() for line in envfile]
                environ = get_filtered_environment(lines, environ=environ)
        #
        #  Otherwise, the rule is an explicit variable assignment
        #   VAR=VAL. If =VAL is not provided then VAL refers to the
        #   value for VAR in the current environment of this process.
        #
        #  Quoted shell variables are expanded using values from the
        #   built environment, not the process environment. So
        #   --env=PATH=/bin --env=PATH='$PATH:/foo' results in
        #   PATH=/bin:/foo.
        #
        else:
            var, *rest = rule.split("=", 1)
            if not rest:
                #
                #  VAR alone with no set value pulls in all matching
                #   variables from current environment that are not already
                #   in the generated environment.
                env = filter_dict(os.environ, var, reverseMatch=False)
                for key, value in env.items():
                    if key not in environ:
                        environ[key] = value
            else:
                #
                #  Template lookup: use jobspec environment first, fallback
                #   to current process environment using ChainMap:
                lookup = ChainMap(environ, os.environ)
                try:
                    environ[var] = Template(rest[0]).substitute(lookup)
                except ValueError as ex:
                    LOGGER.error("--env: Unable to substitute %s", rule)
                    raise
                except KeyError as ex:
                    raise Exception(f"--env: Variable {ex} not found in {rule}")
    return environ


class EnvFileAction(argparse.Action):
    """Convenience class to handle --env-file option

    Append --env-file options to the "env" list in namespace, with "^"
    prepended to the rule to indicate further rules are to be read
    from the indicated file.

    This is required to preserve ordering between the --env and --env-file
    and --env-remove options.
    """

    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, "env", [])
        if items is None:
            items = []
        items.append("^" + values)
        setattr(namespace, "env", items)


class EnvFilterAction(argparse.Action):
    """Convenience class to handle --env-remove option

    Append --env-remove options to the "env" list in namespace, with "-"
    prepended to the option argument.

    This is required to preserve ordering between the --env and --env-remove
    options.
    """

    def __call__(self, parser, namespace, values, option_string=None):
        items = getattr(namespace, "env", [])
        if items is None:
            items = []
        items.append("-" + values)
        setattr(namespace, "env", items)


class MiniCmd:
    """
    MiniCmd is the base class for all flux-mini subcommands
    """

    def __init__(self, **kwargs):
        self.parser = self.create_parser(kwargs)

    @staticmethod
    def create_parser(exclude_io=False):
        """
        Create default parser with args for mini subcommands
        """
        parser = argparse.ArgumentParser(add_help=False)
        parser.add_argument(
            "-t",
            "--time-limit",
            type=str,
            metavar="FSD",
            help="Time limit in Flux standard duration, e.g. 2d, 1.5h",
        )
        parser.add_argument(
            "--urgency",
            help="Set job urgency (0-31, default=16)",
            type=int,
            metavar="N",
            default=16,
        )
        parser.add_argument(
            "--job-name",
            type=str,
            help="Set an optional name for job to NAME",
            metavar="NAME",
        )
        parser.add_argument(
            "-o",
            "--setopt",
            action="append",
            help="Set shell option OPT. An optional value is supported with"
            + " OPT=VAL (default VAL=1) (multiple use OK)",
            metavar="OPT",
        )
        parser.add_argument(
            "--setattr",
            action="append",
            help="Set job attribute ATTR to VAL (multiple use OK)",
            metavar="ATTR=VAL",
        )
        parser.add_argument(
            "--env",
            action="append",
            help="Control how environment variables are exported. If RULE "
            + "starts with '-' apply rest of RULE as a remove filter (see "
            + "--env-remove), if '^' then read rules from a file "
            + "(see --env-file). Otherwise, set matching environment variables "
            + "from the current environment (--env=PATTERN) or set a value "
            + "explicitly (--env=VAR=VALUE). Rules are applied in the order "
            + "they are used on the command line. (multiple use OK)",
            metavar="RULE",
        )
        parser.add_argument(
            "--env-remove",
            action=EnvFilterAction,
            help="Remove environment variables matching PATTERN. "
            + "If PATTERN starts with a '/', then it is matched "
            + "as a regular expression, otherwise PATTERN is a shell "
            + "glob expression. (multiple use OK)",
            metavar="PATTERN",
        )
        parser.add_argument(
            "--env-file",
            action=EnvFileAction,
            help="Read a set of environment rules from FILE. (multiple use OK)",
            metavar="FILE",
        )
        parser.add_argument(
            "--input",
            type=str,
            help="Redirect job stdin from FILENAME, bypassing KVS"
            if not exclude_io
            else argparse.SUPPRESS,
            metavar="FILENAME",
        )
        parser.add_argument(
            "--output",
            type=str,
            help="Redirect job stdout to FILENAME, bypassing KVS"
            if not exclude_io
            else argparse.SUPPRESS,
            metavar="FILENAME",
        )
        parser.add_argument(
            "--error",
            type=str,
            help="Redirect job stderr to FILENAME, bypassing KVS"
            if not exclude_io
            else argparse.SUPPRESS,
            metavar="FILENAME",
        )
        parser.add_argument(
            "-l",
            "--label-io",
            action="store_true",
            help="Add rank labels to stdout, stderr lines"
            if not exclude_io
            else argparse.SUPPRESS,
        )
        parser.add_argument(
            "--flags",
            action="append",
            help="Set comma separated list of job submission flags. Possible "
            + "flags:  debug, waitable",
            metavar="FLAGS",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Don't actually submit job, just emit jobspec",
        )
        parser.add_argument(
            "--debug-emulate", action="store_true", help=argparse.SUPPRESS
        )
        return parser

    def init_jobspec(self, args):
        """
        Return initialized jobspec. This is an abstract method which must
        be provided by each base class
        """
        raise NotImplementedError()

    # pylint: disable=too-many-branches,too-many-statements
    def submit(self, args):
        """
        Submit job, constructing jobspec from args.
        Returns jobid.
        """
        jobspec = self.init_jobspec(args)
        jobspec.cwd = os.getcwd()
        jobspec.environment = get_filtered_environment(args.env)
        if args.time_limit is not None:
            jobspec.duration = args.time_limit

        if args.job_name is not None:
            jobspec.setattr("system.job.name", args.job_name)

        if args.input is not None:
            jobspec.stdin = args.input

        if args.output is not None and args.output not in ["none", "kvs"]:
            jobspec.stdout = args.output
            if args.label_io:
                jobspec.setattr_shell_option("output.stdout.label", True)

        if args.error is not None:
            jobspec.stderr = args.error
            if args.label_io:
                jobspec.setattr_shell_option("output.stderr.label", True)

        if args.setopt is not None:
            for keyval in args.setopt:
                # Split into key, val with a default for 1 if no val given:
                key, val = (keyval.split("=", 1) + [1])[:2]
                try:
                    val = json.loads(val)
                except (json.JSONDecodeError, TypeError):
                    pass
                jobspec.setattr_shell_option(key, val)

        if args.debug_emulate:
            debugged.set_mpir_being_debugged(1)

        if debugged.get_mpir_being_debugged() == 1:
            # if stop-tasks-in-exec is present, overwrite
            jobspec.setattr_shell_option("stop-tasks-in-exec", json.loads("1"))

        if args.setattr is not None:
            for keyval in args.setattr:
                tmp = keyval.split("=", 1)
                if len(tmp) != 2:
                    raise ValueError("--setattr: Missing value for attr " + keyval)
                key = tmp[0]
                try:
                    val = json.loads(tmp[1])
                except (json.JSONDecodeError, TypeError):
                    val = tmp[1]
                jobspec.setattr(key, val)

        arg_debug = False
        arg_waitable = False
        if args.flags is not None:
            for tmp in args.flags:
                for flag in tmp.split(","):
                    if flag == "debug":
                        arg_debug = True
                    elif flag == "waitable":
                        arg_waitable = True
                    else:
                        raise ValueError("--flags: Unknown flag " + flag)

        if args.dry_run:
            print(jobspec.dumps(), file=sys.stdout)
            sys.exit(0)

        flux_handle = flux.Flux()
        jobid = job.submit(
            flux_handle,
            jobspec.dumps(),
            urgency=args.urgency,
            waitable=arg_waitable,
            debug=arg_debug,
        )
        return JobID(jobid)

    def get_parser(self):
        return self.parser


class SubmitCmd(MiniCmd):
    """
    SubmitCmd submits a job, displays the jobid on stdout, and returns.

    Usage: flux mini submit [OPTIONS] cmd ...
    """

    def __init__(self):
        super().__init__()
        self.parser.add_argument(
            "-N", "--nodes", type=int, metavar="N", help="Number of nodes to allocate"
        )
        self.parser.add_argument(
            "-n",
            "--ntasks",
            type=int,
            metavar="N",
            default=1,
            help="Number of tasks to start",
        )
        self.parser.add_argument(
            "-c",
            "--cores-per-task",
            type=int,
            metavar="N",
            default=1,
            help="Number of cores to allocate per task",
        )
        self.parser.add_argument(
            "-g",
            "--gpus-per-task",
            type=int,
            metavar="N",
            help="Number of GPUs to allocate per task",
        )
        self.parser.add_argument(
            "command", nargs=argparse.REMAINDER, help="Job command and arguments"
        )

    def init_jobspec(self, args):
        if not args.command:
            raise ValueError("job command and arguments are missing")

        return JobspecV1.from_command(
            args.command,
            num_tasks=args.ntasks,
            cores_per_task=args.cores_per_task,
            gpus_per_task=args.gpus_per_task,
            num_nodes=args.nodes,
        )

    def main(self, args):
        jobid = self.submit(args)
        print(jobid, file=sys.stdout)


class RunCmd(SubmitCmd):
    """
    RunCmd is identical to SubmitCmd, except it attaches the the job
    after submission.  Some additional options are added to modify the
    attach behavior.

    Usage: flux mini run [OPTIONS] cmd ...
    """

    def __init__(self):
        super().__init__()
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
        if args.debug_emulate:
            attach_args.append("--debug-emulate")
        attach_args.append(jobid.f58.encode("utf-8", errors="surrogateescape"))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"] + ":" + os.environ["PATH"]
        os.execvp("flux-job", attach_args)


def add_batch_alloc_args(parser):
    """
    Add "batch"-specific resource allocation arguments to parser object
    which deal in slots instead of tasks.
    """
    parser.add_argument(
        "--broker-opts",
        metavar="OPTS",
        default=None,
        action="append",
        help="Pass options to flux brokers",
    )
    parser.add_argument(
        "-n",
        "--nslots",
        type=int,
        metavar="N",
        help="Number of total resource slots requested."
        + " The size of a resource slot may be specified via the"
        + " -c, --cores-per-slot and -g, --gpus-per-slot options."
        + " The default slot size is 1 core.",
    )
    parser.add_argument(
        "-c",
        "--cores-per-slot",
        type=int,
        metavar="N",
        default=1,
        help="Number of cores to allocate per slot",
    )
    parser.add_argument(
        "-g",
        "--gpus-per-slot",
        type=int,
        metavar="N",
        help="Number of GPUs to allocate per slot",
    )
    parser.add_argument(
        "-N",
        "--nodes",
        type=int,
        metavar="N",
        help="Distribute allocated resource slots across N individual nodes",
    )


def list_split(opts):
    """
    Return a list by splitting each member of opts on ','
    """
    if opts:
        x = chain.from_iterable([x.split(",") for x in opts])
        return list(x)
    return []


class BatchCmd(MiniCmd):
    def __init__(self, **kwargs):
        super().__init__(**kwargs)
        self.parser.add_argument(
            "--wrap",
            action="store_true",
            help="Wrap arguments or stdin in a /bin/sh script",
        )
        add_batch_alloc_args(self.parser)
        self.parser.add_argument(
            "SCRIPT",
            nargs=argparse.REMAINDER,
            help="Batch script and arguments to submit",
        )

    @staticmethod
    def read_script(args):
        if args.SCRIPT:
            if args.wrap:
                #  Wrap args in /bin/sh script
                return "#!/bin/sh\n" + " ".join(args.SCRIPT) + "\n"

            # O/w, open script for reading
            name = open_arg = args.SCRIPT[0]
        else:
            name = "stdin"
            open_arg = 0  # when passed to `open`, 0 gives the `stdin` stream
        with open(open_arg, "r", encoding="utf-8") as filep:
            try:
                #  Read script
                script = filep.read()
                if args.wrap:
                    script = "#!/bin/sh\n" + script
            except UnicodeError:
                raise ValueError(
                    f"{name} does not appear to be a script, "
                    "or failed to encode as utf-8"
                )
            return script

    def init_jobspec(self, args):
        # If no script (reading from stdin), then use "flux" as arg[0]
        if not args.nslots:
            raise ValueError("Number of slots to allocate must be specified")

        jobspec = JobspecV1.from_batch_command(
            script=self.read_script(args),
            jobname=args.SCRIPT[0] if args.SCRIPT else "batchscript",
            args=args.SCRIPT[1:],
            num_slots=args.nslots,
            cores_per_slot=args.cores_per_slot,
            gpus_per_slot=args.gpus_per_slot,
            num_nodes=args.nodes,
            broker_opts=list_split(args.broker_opts),
        )

        # Default output is flux-{{jobid}}.out
        # overridden by either --output=none or --output=kvs
        if not args.output:
            jobspec.stdout = "flux-{{id}}.out"
        return jobspec

    def main(self, args):
        jobid = self.submit(args)
        print(jobid, file=sys.stdout)


class AllocCmd(MiniCmd):
    def __init__(self):
        super().__init__(exclude_io=True)
        add_batch_alloc_args(self.parser)
        self.parser.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=0,
            help="Increase verbosity on stderr (multiple use OK)",
        )
        self.parser.add_argument(
            "COMMAND",
            nargs=argparse.REMAINDER,
            help="Set the initial COMMAND of new Flux instance."
            + "(default is an interactive shell)",
        )

    def init_jobspec(self, args):

        if not args.nslots:
            raise ValueError("Number of slots to allocate must be specified")

        jobspec = JobspecV1.from_nest_command(
            command=args.COMMAND,
            num_slots=args.nslots,
            cores_per_slot=args.cores_per_slot,
            gpus_per_slot=args.gpus_per_slot,
            num_nodes=args.nodes,
            broker_opts=list_split(args.broker_opts),
        )
        if sys.stdin.isatty():
            jobspec.setattr_shell_option("pty", True)
        return jobspec

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
        attach_args.append(jobid.f58.encode("utf-8", errors="surrogateescape"))

        # Exec flux-job attach, searching for it in FLUX_EXEC_PATH.
        os.environ["PATH"] = os.environ["FLUX_EXEC_PATH"] + ":" + os.environ["PATH"]
        os.execvp("flux-job", attach_args)


LOGGER = logging.getLogger("flux-mini")


@util.CLIMain(LOGGER)
def main():

    sys.stdout = open(
        sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )
    sys.stderr = open(
        sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
    )

    parser = argparse.ArgumentParser(prog="flux-mini")
    subparsers = parser.add_subparsers(
        title="supported subcommands", description="", dest="subcommand"
    )
    subparsers.required = True

    # run
    run = RunCmd()
    mini_run_parser_sub = subparsers.add_parser(
        "run",
        parents=[run.get_parser()],
        help="run a job interactively",
        formatter_class=flux.util.help_formatter(),
    )
    mini_run_parser_sub.set_defaults(func=run.main)

    # submit
    submit = SubmitCmd()
    mini_submit_parser_sub = subparsers.add_parser(
        "submit",
        parents=[submit.get_parser()],
        help="enqueue a job",
        formatter_class=flux.util.help_formatter(),
    )
    mini_submit_parser_sub.set_defaults(func=submit.main)

    # batch
    batch = BatchCmd()
    description = """
    Submit a batch SCRIPT and ARGS to be run as the initial program of
    a Flux instance.  If no batch script is provided, one will be read
    from stdin.
    """
    mini_batch_parser_sub = subparsers.add_parser(
        "batch",
        parents=[batch.get_parser()],
        help="enqueue a batch script",
        usage="flux mini batch [OPTIONS...] [SCRIPT] [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
    )
    mini_batch_parser_sub.set_defaults(func=batch.main)

    # alloc
    alloc = AllocCmd()
    description = """
    Allocate resources and start a new Flux instance. Once the instance
    has started, attach to it interactively.
    """
    mini_alloc_parser_sub = subparsers.add_parser(
        "alloc",
        parents=[alloc.get_parser()],
        help="allocate a new instance for interactive use",
        usage="flux mini alloc [COMMAND] [ARGS...]",
        description=description,
        formatter_class=flux.util.help_formatter(),
    )
    mini_alloc_parser_sub.set_defaults(func=alloc.main)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()

# vi: ts=4 sw=4 expandtab
