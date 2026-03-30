##############################################################
# Copyright 2023 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

# A command base for all user facing commands for different
# kinds of submission. This used to be a base in flux-mini
# and now is used as a module with shared logic.

import argparse
import atexit
import logging
import os
import sys

import flux
from flux import debugged, job, util
from flux.cli.plugin import CLIPluginRegistry
from flux.idset import IDset
from flux.job import JobspecV1, JobWatcher
from flux.job._utils import BatchConfig
from flux.progress import ProgressBar

LOGGER = logging.getLogger("flux")


class BeginTimeAction(argparse.Action):
    """Convenience class to handle --begin-time file option

    Append --begin-time options to the "dependency" list in namespace
    """

    def __call__(self, parser, namespace, values, option_string=None):
        uri = "begin-time:" + str(util.parse_datetime(values).timestamp())
        items = getattr(namespace, "dependency", [])
        if items is None:
            items = []
        items.append(uri)
        setattr(namespace, "dependency", items)


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


class ConfAction(argparse.Action):
    """Handle batch/alloc --conf option"""

    def __call__(self, parser, namespace, values, option_string=None):
        conf = getattr(namespace, "conf", None)
        if conf is None:
            conf = BatchConfig()
            setattr(namespace, "conf", conf)
        conf.update(values)


class Xcmd:
    """Represent a Flux job with mutable command and option args"""

    # dict of mutable argparse args. The values are used in
    #  the string representation of an Xcmd object.
    mutable_args = {
        "queue": "-q",
        "bank": "-B",
        "ntasks": "-n",
        "nodes": "-N",
        "cores_per_task": "-c",
        "gpus_per_task": "-g",
        "cores": "--cores=",
        "tasks_per_node": "--tasks-per-node=",
        "tasks_per_core": "--tasks-per-core=",
        "gpus_per_node": "--gpus-per-node=",
        "time_limit": "-t",
        "env": "--env=",
        "env_file": "--env-file=",
        "env_remove": "--env-remove=",
        "urgency": "--urgency=",
        "setopt": "-o ",
        "setattr": "--setattr=",
        "job_name": "--job-name=",
        "input": "--input=",
        "output": "--output=",
        "error": "--error=",
        "cc": "--cc=",
        "bcc": "--bcc=",
        "log": "--log=",
        "log_stderr": "--log-stderr=",
        "dependency": "--dependency=",
        "taskmap": "--taskmap=",
        "requires": "--requires=",
        "wait": "--wait-event=",
        "cwd": "--cwd=",
        "flags": "--flags=",
        "begin_time": "--begin-time=",
        "signal": "--signal=",
    }

    class Xinput:
        """A string class with convenient attributes for formatting args

        This class represents a string with special attributes specific
        for use on the bulksubmit command line, e.g.::

            {0.%}    : the argument without filename extension
            {0./}    : the argument basename
            {0.//}   : the argument dirname
            {0./%}   : the basename without filename extension
            {0.name} : the result of dynamically assigned method "name"

        """

        def __init__(self, arg, methods):
            self.methods = methods
            self.string = arg

        def __str__(self):
            return self.string

        def __getattr__(self, attr):
            if attr == "%":
                return os.path.splitext(self.string)[0]
            if attr == "/":
                return os.path.basename(self.string)
            if attr == "//":
                return os.path.dirname(self.string)
            if attr == "/%":
                return os.path.basename(os.path.splitext(self.string)[0])
            if attr in self.methods:
                #  Note: combine list return values with the special
                #   sentinel ::list::: so they can be split up again
                #   after .format() converts them to strings. This allows
                #   user-provided methods to return lists as well as
                #   single values, where each list element can become
                #   a new argument in a command
                #
                # pylint: disable=eval-used
                result = eval(self.methods[attr], globals(), dict(x=self.string))
                if isinstance(result, list):
                    return "::list::".join(result)
                return result
            raise ValueError(f"Unknown input string method '.{attr}'")

    @staticmethod
    def preserve_mustache(val):
        """Preserve any mustache template in value 'val'"""

        def subst(val):
            return val.replace("{{", "=stache=").replace("}}", "=/stache=")

        if isinstance(val, str):
            return subst(val)
        if isinstance(val, list):
            return [subst(x) for x in val]
        return val

    @staticmethod
    def restore_mustache(val):
        """Restore any mustache template in value 'val'"""

        def restore(val):
            return val.replace("=stache=", "{{").replace("=/stache=", "}}")

        if isinstance(val, str):
            return restore(val)
        if isinstance(val, list):
            return [restore(x) for x in val]
        return val

    def __init__(self, args, inputs=None, **kwargs):
        """Initialize and Xcmd (eXtensible Command) object

        Given BulkSubmit `args` and `inputs`, substitute all inputs
        in command and applicable options using string.format().

        """
        if inputs is None:
            inputs = []

        #  Save reference to original args:
        self._orig_args = args

        #  Convert all inputs to Xinputs so special attributes are
        #   available during .format() processing:
        #
        inputs = [self.Xinput(x, args.methods) for x in inputs]

        #  Format each argument in args.command, splitting on the
        #   special "::list::" sentinel to handle the case where
        #   custom input methods return a list (See Xinput.__getattr__)
        #
        self.command = []
        for arg in args.command:
            try:
                val = self.preserve_mustache(arg)
                result = val.format(*inputs, **kwargs).split("::list::")
                newval = self.restore_mustache(result)
            except (IndexError, KeyError):
                LOGGER.error("Invalid replacement string in command: '%s'", arg)
                sys.exit(1)
            if newval:
                self.command.extend(newval)

        #  Format all supported mutable options defined in `mutable_args`
        #  Note: only list and string options are supported.
        #
        self.modified = {}
        for attr in self.mutable_args:
            val = getattr(args, attr)
            if val is None:
                continue

            val = self.preserve_mustache(val)

            try:
                if isinstance(val, str):
                    newval = val.format(*inputs, **kwargs)
                elif isinstance(val, list):
                    newval = [x.format(*inputs, **kwargs) for x in val]
                else:
                    newval = val
            except IndexError:
                LOGGER.error(
                    "Invalid replacement index in %s%s'",
                    self.mutable_args[attr],
                    val,
                )
                sys.exit(1)
            except KeyError as exc:
                LOGGER.error(
                    "Replacement key %s not found in '%s%s'",
                    exc,
                    self.mutable_args[attr],
                    val,
                )
                sys.exit(1)

            newval = self.restore_mustache(newval)

            setattr(self, attr, newval)

            #  For better verbose and dry-run output, capture mutable
            #   args that were actually changed:
            if val != newval or attr == "cc":
                self.modified[attr] = True

    def __getattr__(self, attr):
        """
        Fall back to original args if attribute not found.
        This allows an Xcmd object to used in place of an argparse Namespace.
        """
        return getattr(self._orig_args, attr)

    def __str__(self):
        """String representation of an Xcmd for debugging output"""
        result = []
        for attr in self.mutable_args:
            value = getattr(self, attr)
            if attr in self.modified and value:
                opt = self.mutable_args[attr]
                result.append(f"{opt}{value}")
        result.extend(self.command)
        return " ".join(result)


class MiniCmd:
    """
    MiniCmd is the base class for all flux submission subcommands
    """

    def __init__(self, prog, usage=None, description=None, exclude_io=False):
        self.prog = prog
        self.flux_handle = None
        self.exitcode = 0
        self.progress = None
        self.watcher = None
        self.plugins = CLIPluginRegistry(prog)
        self.parser = self.create_parser(prog, usage, description, exclude_io)

    def run_command(self):
        """
        Convenience method containing common code to run a command derived
        from the MiniCmd base class.

        This function reopens standard output and error in a mode suitable
        for utf8 output, initializes the command's ArgParse parser, parses
        command line arguments and finally executes ``self.main(args)``.
        """
        sys.stdout = open(
            sys.stdout.fileno(), "w", encoding="utf8", errors="surrogateescape"
        )
        sys.stderr = open(
            sys.stderr.fileno(), "w", encoding="utf8", errors="surrogateescape"
        )
        parser = self.get_parser()

        # ensure plugins argument group comes last in `--help` output:
        group = parser.add_argument_group(
            "Options provided by plugins "
            + "(--help=OPTION for extended plugin documentation)"
        )
        for option in self.plugins.options:
            group.add_argument(option.name, **option.kwargs)

        args = parser.parse_args()
        if args.help == "default":
            parser.print_help()
            sys.exit(0)
        elif args.help is not None:
            self.plugins.print_help(args.help)

        self.main(args)

    @staticmethod
    def create_parser(
        prog, usage=None, description=None, exclude_io=False, add_help=True
    ):
        """
        Create default parser with args for submission subcommands
        Args:
            prog (str): program name in usage output
            usage (str, optional): usage string, by default
                ``{prog} [OPTIONS...] COMMAND [ARGS...]``
            description (str, optional): short description of command to
                follow usage. May be multiple lines.
        """
        if usage is None:
            usage = f"{prog} [OPTIONS...] COMMAND [ARGS...]"
        parser = argparse.ArgumentParser(
            prog=prog,
            usage=usage,
            description=description,
            formatter_class=flux.util.help_formatter(),
            add_help=False,
        )
        parser.add_argument(
            "--help",
            nargs="?",
            const="default",
            metavar="TOPIC",
            help="Show this help message or extended help for TOPIC and exit",
        )
        parser.add_argument(
            "-B",
            "--bank",
            type=str,
            metavar="BANK",
            help="Submit a job to a specific named bank",
        )
        parser.add_argument(
            "-q",
            "--queue",
            type=str,
            metavar="NAME",
            help="Submit a job to a specific named queue",
        )
        parser.add_argument(
            "-t",
            "--time-limit",
            type=str,
            metavar="MIN|FSD",
            help="Time limit in minutes when no units provided, otherwise "
            + "in Flux standard duration, e.g. 30s, 2d, 1.5h",
        )
        parser.add_argument(
            "--urgency",
            help="Set job urgency (0-31), hold=0, default=16, expedite=31",
            metavar="N",
            default="16",
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
            "-S",
            "--setattr",
            action="append",
            help="Set job attribute ATTR. An optional value is supported "
            + " with ATTR=VAL (default VAL=1). If ATTR starts with ^, "
            + "then VAL is a file containing valid JSON which will be used "
            + "as the value of the attribute. (multiple use OK)",
            metavar="ATTR",
        )
        parser.add_argument(
            "--add-file",
            action="append",
            help="Add a file at PATH with optional NAME to jobspec. The "
            + "file will be extracted to {{tmpdir}}/NAME. If NAME is not "
            + "specified, then the basename of PATH will be used. If "
            + "necessary, permissions may be specified via NAME:PERMS. "
            + "(multiple use OK)",
            metavar="[NAME=]PATH",
        )
        parser.add_argument(
            "--dependency",
            action="append",
            help="Set an RFC 26 dependency URI for this job",
            metavar="URI",
        )
        parser.add_argument(
            "--requires",
            action="append",
            help="Specify job constraints in RFC 35 syntax",
            metavar="CONSTRAINT",
        )
        parser.add_argument(
            "--begin-time",
            action=BeginTimeAction,
            metavar="+FSD|TIME",
            help="Set minimum start time as offset in FSD (e.g. +1h) or "
            + 'an absolute TIME (e.g. "3pm") for job',
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
            "--rlimit",
            action="append",
            help="Control how soft resource limits are propagated to the job. "
            + "If RULE starts with a '-', then do not propagate matching "
            + "resource limits (e.g. '-*' propagates nothing). Otherwise, "
            + "propagate the current limit or a specific value, e.g. "
            + "--rlimit=core or --rlimit=core=16. The option may be used "
            + "multiple times to build a reduced set of propagated limits, "
            + "e.g. --rlimit=-*,core will only propagate RLIMIT_CORE.",
            metavar="RULE",
        )
        parser.add_argument(
            "--input",
            type=str,
            help=(
                "Redirect job stdin from FILENAME, bypassing KVS"
                if not exclude_io
                else argparse.SUPPRESS
            ),
            metavar="FILENAME",
        )
        parser.add_argument(
            "--output",
            type=str,
            help=(
                "Redirect job stdout to FILENAME, bypassing KVS"
                if not exclude_io
                else argparse.SUPPRESS
            ),
            metavar="FILENAME",
        )
        parser.add_argument(
            "--error",
            type=str,
            help=(
                "Redirect job stderr to FILENAME, bypassing KVS"
                if not exclude_io
                else argparse.SUPPRESS
            ),
            metavar="FILENAME",
        )
        parser.add_argument(
            "-u",
            "--unbuffered",
            action="store_true",
            help="Disable buffering of input and output",
        )
        parser.add_argument(
            "-l",
            "--label-io",
            action="store_true",
            help=(
                "Add rank labels to stdout, stderr lines"
                if not exclude_io
                else argparse.SUPPRESS
            ),
        )
        parser.add_argument(
            "--cwd", help="Set job working directory", metavar="DIRECTORY"
        )
        parser.add_argument(
            "--flags",
            action="append",
            help="Set comma separated list of job submission flags. Possible "
            + "flags:  debug, waitable, novalidate",
            metavar="FLAGS",
        )
        parser.add_argument(
            "--signal",
            help="Schedule delivery of signal SIG at a defined TIME before "
            + "job expiration. Default SIG is SIGUSR1, default TIME is 60s.",
            metavar="[SIG][@TIME]",
        )
        parser.add_argument(
            "--dry-run",
            action="store_true",
            help="Don't actually submit job, just emit jobspec",
        )
        parser.add_argument(
            "--quiet",
            action="store_true",
            help="Do not print jobid to stdout on submission",
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

    def jobspec_create(self, args):
        """
        Create a jobspec from args and return it to caller
        """
        # allow plugins to initialize args
        self.plugins.preinit(args)

        jobspec = self.init_jobspec(args)

        # Apply job options (env, rlimits, signal, taskmap, deps,
        # constraints, setopt, setattr, add-file, plugins).
        # _preinit=False because preinit() was already called above.
        jobspec.apply_options(args, prog=self.prog, _preinit=False)

        # MPIR debugging support (CLI-only: sets global debugger state)
        if getattr(args, "debug_emulate", False):
            debugged.set_mpir_being_debugged(1)
        if debugged.get_mpir_being_debugged() == 1:
            jobspec.setattr_shell_option("stop-tasks-in-exec", 1)

        return jobspec

    def submit_async(self, args, jobspec=None):
        """
        Submit job, constructing jobspec from args unless jobspec is not None.
        Returns a SubmitFuture.
        """
        if jobspec is None:
            jobspec = self.jobspec_create(args)

        if args.dry_run:
            print(jobspec.dumps(), file=sys.stdout)
            sys.exit(0)

        arg_debug = False
        arg_waitable = False
        arg_novalidate = False
        if args.flags is not None:
            for tmp in args.flags:
                for flag in tmp.split(","):
                    if flag == "debug":
                        arg_debug = True
                    elif flag == "waitable":
                        arg_waitable = True
                    elif flag == "novalidate":
                        arg_novalidate = True
                    else:
                        raise ValueError("--flags: Unknown flag " + flag)

        if not self.flux_handle:
            self.flux_handle = flux.Flux()

        if args.urgency == "default":
            urgency = flux.constants.FLUX_JOB_URGENCY_DEFAULT
        elif args.urgency == "hold":
            urgency = flux.constants.FLUX_JOB_URGENCY_HOLD
        elif args.urgency == "expedite":
            urgency = flux.constants.FLUX_JOB_URGENCY_EXPEDITE
        else:
            urgency = int(args.urgency)

        return job.submit_async(
            self.flux_handle,
            jobspec.dumps(),
            urgency=urgency,
            waitable=arg_waitable,
            debug=arg_debug,
            novalidate=arg_novalidate,
        )

    def submit(self, args, jobspec=None):
        return self.submit_async(args, jobspec).get_id()

    def get_parser(self):
        return self.parser


class SubmitBaseCmd(MiniCmd):
    """
    SubmitBaseCmd is an abstract class with shared code for job submission
    """

    def __init__(self, prog, usage=None, description=None):
        super().__init__(prog, usage, description)
        self.parser.add_argument(
            "--taskmap",
            type=str,
            help="Select the scheme for mapping task ids to nodes as a URI "
            + "(i.e. SCHEME[:VALUE]). Value options include block, cyclic, "
            + "cyclic:N, or manual:TASKMAP (default: block)",
            metavar="URI",
        )
        group = self.parser.add_argument_group("Common resource options")
        group.add_argument(
            "-N", "--nodes", metavar="N", help="Number of nodes to allocate"
        )
        group.add_argument(
            "-x",
            "--exclusive",
            action="store_true",
            help="With -N, --nodes, allocate nodes exclusively",
        )
        group = self.parser.add_argument_group(
            "Per task options",
            "The following options allow per-task specification of resources, "
            + "and should not be combined with per-resource options.",
        )
        group.add_argument(
            "-n",
            "--ntasks",
            metavar="N",
            help="Number of tasks to start",
        )
        group.add_argument(
            "-c",
            "--cores-per-task",
            metavar="N",
            help="Number of cores to allocate per task",
        )
        group.add_argument(
            "-g",
            "--gpus-per-task",
            metavar="N",
            help="Number of GPUs to allocate per task",
        )
        group = self.parser.add_argument_group(
            "Per resource options",
            "The following options allow per-resource specification of "
            + "tasks, and should not be used with per-task options above",
        )
        group.add_argument(
            "--cores",
            metavar="N",
            help="Request a total number of cores",
        )
        group.add_argument(
            "--tasks-per-node",
            metavar="N",
            help="Force number of tasks per node",
        )
        group.add_argument(
            "--tasks-per-core",
            metavar="N",
            help="Force number of tasks per core",
        )
        group.add_argument(
            "--gpus-per-node",
            metavar="N",
            help="Request a number of GPUs per node with --nodes",
        )
        self.parser.add_argument(
            "-v",
            "--verbose",
            action="count",
            default=0,
            help="Increase verbosity on stderr (multiple use OK)",
        )

    # pylint: disable=too-many-branches
    def init_jobspec(self, args):
        per_resource_type = None
        per_resource_count = None

        if not args.command:
            raise ValueError("job command and arguments are missing")

        #  Remove first -- from command in case user used it to separate
        #  Flux cli options from command and options
        if args.command[0] == "--":
            args.command.pop(0)

        #  Time limit in cli submission commands defaults to minutes if
        #  no units are given, but JobspecV1 duration is in seconds or FSD,
        #  so convert if necessary:
        if args.time_limit is not None:
            try:
                limit = float(args.time_limit)
                args.time_limit = limit * 60
            except ValueError:
                # no conversion necessary
                pass

        #  Check if --input specified an IDset. If not, then assume a file,
        #  otherwise do not modify jobspec, input will be handled by
        #  `flux job attach`:
        stdin = None
        if args.input is not None:
            try:
                IDset(args.input)
            except (ValueError, OSError):
                stdin = args.input

        #  Ensure integer args are converted to int() here.
        #  This is done because we do not use type=int in argparse in order
        #   to allow these options to be mutable for bulksubmit:
        #
        for arg in [
            "ntasks",
            "nodes",
            "cores",
            "cores_per_task",
            "gpus_per_task",
            "tasks_per_node",
            "tasks_per_core",
            "gpus_per_node",
        ]:
            value = getattr(args, arg)
            if value:
                try:
                    setattr(args, arg, int(value))
                except ValueError:
                    opt = arg.replace("_", "-")
                    raise ValueError(f"--{opt}: invalid int value '{value}'")

        if args.tasks_per_node is not None and args.tasks_per_core is not None:
            raise ValueError(
                "Do not specify both the number of tasks per node and per core"
            )

        #  Handle --tasks-per-node or --tasks-per-core (it is an error to
        #   specify both). Check options for validity and assign the
        #   per_resource variable when valid.
        #
        if args.tasks_per_node is not None or args.tasks_per_core is not None:
            if args.tasks_per_node is not None:
                if args.tasks_per_node < 1:
                    raise ValueError("--tasks-per-node must be >= 1")

                per_resource_type = "node"
                per_resource_count = args.tasks_per_node
            elif args.tasks_per_core is not None:
                if args.tasks_per_core < 1:
                    raise ValueError("--tasks-per-core must be >= 1")
                per_resource_type = "core"
                per_resource_count = args.tasks_per_core

        if args.gpus_per_node:
            if not args.nodes:
                raise ValueError("--gpus-per-node requires --nodes")

        #  If any of --tasks-per-node, --tasks-per-core, --cores, or
        #   --gpus-per-node is used, then use the per_resource constructor:
        #
        if (
            per_resource_type is not None
            or args.gpus_per_node is not None
            or args.cores is not None
        ):
            #  If any of the per-task options was also specified, raise an
            #   error here instead of silently ignoring those options:
            if (
                args.ntasks is not None
                or args.cores_per_task is not None
                or args.gpus_per_task
            ):
                raise ValueError(
                    "Per-resource options can't be used with per-task options."
                    + " (See --help for details)"
                )

            #  In per-resource mode, set the exclusive flag if nodes is
            #   specified without cores. This preserves the default behavior
            #   of requesting nodes exclusively when only -N is used:
            if args.nodes and args.cores is None:
                args.exclusive = True

            return JobspecV1.per_resource(
                args.command,
                ncores=args.cores,
                nnodes=args.nodes,
                per_resource_type=per_resource_type,
                per_resource_count=per_resource_count,
                gpus_per_node=args.gpus_per_node,
                exclusive=args.exclusive,
                duration=args.time_limit,
                cwd=args.cwd if args.cwd is not None else os.getcwd(),
                name=args.job_name,
                input=stdin,
                output=args.output,
                error=args.error,
                label_io=args.label_io,
                unbuffered=args.unbuffered,
                queue=args.queue,
                bank=args.bank,
            )

        #  If ntasks not set, then set it to node count, with
        #   exclusive flag enabled
        if not args.ntasks and args.nodes:
            args.ntasks = args.nodes
            args.exclusive = True

        # O/w default ntasks for from_command() is 1:
        if not args.ntasks:
            args.ntasks = 1

        # default cores_per_task for from_command() is 1:
        if not args.cores_per_task:
            args.cores_per_task = 1

        return JobspecV1.from_command(
            args.command,
            num_tasks=args.ntasks,
            cores_per_task=args.cores_per_task,
            gpus_per_task=args.gpus_per_task,
            num_nodes=args.nodes,
            exclusive=args.exclusive,
            duration=args.time_limit,
            cwd=args.cwd if args.cwd is not None else os.getcwd(),
            name=args.job_name,
            input=stdin,
            output=args.output,
            error=args.error,
            label_io=args.label_io,
            unbuffered=args.unbuffered,
            queue=args.queue,
            bank=args.bank,
        )

    def run_and_exit(self):
        self.flux_handle.reactor_run()
        if self.watcher:
            self.exitcode = max(self.watcher.exitcode, self.exitcode)
        sys.exit(self.exitcode)


class SubmitBulkCmd(SubmitBaseCmd):
    """
    SubmitBulkCmd adds options for submitting copies of jobs,
    watching progress of submission, and waiting for job completion
    to the SubmitBaseCmd class
    """

    def __init__(self, prog, usage=None, description=None):
        #  dictionary of open logfiles for --log, --log-stderr:
        self._logfiles = {}
        self.t0 = None

        super().__init__(prog, usage, description)
        self.parser.add_argument(
            "--cc",
            metavar="IDSET",
            default=None,
            help="Replicate job for each ID in IDSET. "
            "(FLUX_JOB_CC=ID will be set for each job submitted)",
        )
        self.parser.add_argument(
            "--bcc",
            metavar="IDSET",
            default=None,
            help="Like --cc, but FLUX_JOB_CC is not set",
        )
        self.parser.add_argument(
            "--wait-event",
            metavar="NAME",
            dest="wait",
            help="Wait for event NAME for all jobs after submission",
        )
        self.parser.add_argument(
            "--wait",
            action="store_const",
            const="clean",
            help="Wait for all jobs to complete after submission "
            "(same as --wait-event=clean)",
        )
        self.parser.add_argument(
            "--watch",
            action="store_true",
            help="Watch all job output (implies --wait)",
        )
        self.parser.add_argument(
            "--log",
            metavar="FILE",
            help="Print program log messages (e.g. submitted jobid) to FILE "
            "instead of terminal",
        )
        self.parser.add_argument(
            "--log-stderr",
            metavar="FILE",
            help="Separate stderr messages into FILE instead of terminal or "
            "logfile destination",
        )
        self.parser.add_argument(
            "--progress",
            action="store_true",
            help="Show progress of job submission or completion (with --wait)",
        )
        self.parser.add_argument(
            "--jps",
            action="store_true",
            help="With --progress, show job throughput",
        )

    def submit_cb(self, future, args, label=""):
        try:
            jobid = future.get_id()
            if not args.quiet:
                print(jobid, file=args.stdout)
        except OSError as exc:
            print(f"{label}{exc}", file=args.stderr)
            self.exitcode = 1
            self.progress_update(submit_failed=True)
            return

        if self.watcher:
            self.watcher.add_jobid(jobid, args.stdout, args.stderr, args.wait)
        elif self.progress:
            #  Update progress of submission only
            self.progress.update(jps=self.jobs_per_sec())

    def _progress_check(self, args):
        if args.progress and not self.progress and not sys.stdout.isatty():
            LOGGER.warning("stdout is not a tty. Ignoring --progress option")
            args.progress = None

    def watcher_start(self, args):
        if not self.watcher:
            #  Need to open self.flux_handle if it isn't already in order
            #  to start the watcher
            if not self.flux_handle:
                self.flux_handle = flux.Flux()

            self._progress_check(args)

            self.watcher = JobWatcher(
                self.flux_handle,
                progress=args.progress,
                jps=args.jps,
                log_events=(args.verbose > 1),
                log_status=(args.verbose > 0),
                labelio=args.label_io,
                wait=args.wait,
                watch=args.watch,
            ).start()

    def jobs_per_sec(self):
        return (self.progress.count + 1) / self.progress.elapsed

    def progress_start(self, args, total):
        """
        Initialize job submission progress bar if user requested --progress
        without --wait or --watch
        """
        self._progress_check(args)
        if not args.progress or self.progress:
            # progress bar not requested or already started
            return

        if args.wait or args.watch:
            # progress handled in JobWatcher class
            return

        before = "Submitting {total} jobs: "
        after = "{percent:5.1f}% {elapsed.dt}"
        if args.jps:
            after = "{percent:5.1f}% {jps:4.1f} job/s"
        self.progress = ProgressBar(
            timer=False,
            total=total,
            width=len(str(total)),
            before=before,
            after=after,
            pending=0,
            fail=0,
            jps=0,
        ).start()

    def progress_update(self, jobinfo=None, submit=False, submit_failed=False):
        """
        Update submission progress bar if one was requested
        """
        if not self.progress:
            return

        if not self.progress.timer:
            #  Start a timer to update progress bar without other events
            #  (we have to do it here since a flux handle does not exist
            #   in progress_start). We use 250ms to make the progress bar
            #   more fluid.
            timer = self.flux_handle.timer_watcher_create(
                0, lambda *x: self.progress.redraw(), repeat=0.25
            ).start()
            self.progress.update(advance=0, timer=timer)

            #  Don't let this timer watcher contribute to the reactor's
            #   "active" reference count:
            timer.unref()

        if submit:
            self.progress.update(
                advance=0,
                pending=self.progress.pending + 1,
            )
        elif submit_failed:
            self.progress.update(
                advance=1,
                pending=self.progress.pending - 1,
                fail=self.progress.fail + 1,
                jps=self.jobs_per_sec(),
            )

    @staticmethod
    def cc_list(args):
        """
        Return a list of values representing job copies given by --cc/--bcc
        """
        cclist = [""]
        if args.cc and args.bcc:
            raise ValueError("specify only one of --cc or --bcc")
        if args.cc:
            cclist = IDset(args.cc)
        elif args.bcc:
            cclist = IDset(args.bcc)
        return cclist

    def openlog(self, filename):
        if filename not in self._logfiles:
            filep = open(filename, "w", buffering=1)
            atexit.register(lambda x: x.close(), filep)
            self._logfiles[filename] = filep
        return self._logfiles[filename]

    def submit_async_with_cc(self, args, cclist=None):
        """
        Asynchronously submit jobs, optionally submitting a copy of
        each job for each member of a cc-list. If the cclist is not
        passed in to the method, then one is created from either
        --cc or --bcc options.
        """
        if not cclist:
            cclist = self.cc_list(args)
        label = ""

        #  Save default stdout/err location in args so it can be overridden
        #   by --log and --log-stderr and the correct location is available
        #   in each job's callback chain:
        #
        args.stdout = sys.stdout
        args.stderr = sys.stderr

        if args.watch or args.wait:
            self.watcher_start(args)

        elif args.progress:
            self.progress_start(args, len(cclist))

        for i in cclist:
            #  substitute any {cc} in args (only if --cc or --bcc):
            xargs = Xcmd(args, cc=i) if isinstance(i, int) else args
            jobspec = self.jobspec_create(xargs)

            #  For now, an idset argument to args.input is not supported
            #  in submit:
            if xargs.input:
                try:
                    IDset(xargs.input)
                    LOGGER.error("per-task input not supported for submit")
                    sys.exit(1)
                except (ValueError, OSError):
                    # --input was not an idset, just continue:
                    pass

            if args.cc or args.bcc:
                label = f"cc={i}: "
                if not args.bcc:
                    jobspec.environment["FLUX_JOB_CC"] = str(i)

            #  Check for request to redirect program stdout/err
            #  By default, --log redirects both stdout and stderr
            #  (We explicitly don't want these attributes defined in
            #   __init__, o/w we won't fall back to parent args, so
            #   disable pylint warning)
            #  pylint: disable=attribute-defined-outside-init
            if xargs.log:
                xargs.stdout = self.openlog(xargs.log)
                xargs.stderr = xargs.stdout
            if xargs.log_stderr:
                xargs.stderr = self.openlog(xargs.log_stderr)

            self.submit_async(xargs, jobspec).then(self.submit_cb, xargs, label)

    def main(self, args):
        self.submit_async_with_cc(args)
        self.run_and_exit()


class BatchAllocCmd(MiniCmd):
    def __init__(self, prog, usage=None, description=None, exclude_io=True):
        self.t0 = None
        super().__init__(prog, usage, description, exclude_io)
        self.parser.add_argument(
            "--add-brokers", default=0, type=int, help=argparse.SUPPRESS
        )
        self.parser.add_argument(
            "--conf",
            metavar="CONF",
            default=None,
            action=ConfAction,
            help="Set configuration for a child Flux instance. CONF may be a "
            + "multiline string in JSON or TOML, a configuration key=value, a "
            + "path to a JSON or TOML file, or a configuration loaded by name "
            + "from a standard search path. This option may specified multiple "
            + "times, in which case the config is iteratively updated.",
        )
        self.parser.add_argument(
            "--broker-opts",
            metavar="OPTS",
            default=None,
            action="append",
            help="Pass options to flux brokers",
        )
        self.parser.add_argument(
            "--dump",
            nargs="?",
            const="flux-{{jobid}}-dump.tgz",
            metavar="FILE",
            help="Archive KVS on exit",
        )
        self.parser.add_argument(
            "-n",
            "--nslots",
            type=int,
            metavar="N",
            help="Number of total resource slots requested."
            + " The size of a resource slot may be specified via the"
            + " -c, --cores-per-slot and -g, --gpus-per-slot options."
            + " The default slot size is 1 core.",
        )
        self.parser.add_argument(
            "-c",
            "--cores-per-slot",
            type=int,
            metavar="N",
            default=1,
            help="Number of cores to allocate per slot",
        )
        self.parser.add_argument(
            "-g",
            "--gpus-per-slot",
            type=int,
            metavar="N",
            help="Number of GPUs to allocate per slot",
        )
        self.parser.add_argument(
            "-N",
            "--nodes",
            type=int,
            metavar="N",
            help="Distribute allocated resource slots across N individual nodes",
        )
        self.parser.add_argument(
            "-x",
            "--exclusive",
            action="store_true",
            help="With -N, --nodes, allocate nodes exclusively",
        )

    def jobspec_create(self, args):
        # Ensure args.conf is a BatchConfig before preinit() so plugins can
        # call args.conf.update() safely even when --conf was not specified.
        if args.conf is None:
            args.conf = BatchConfig()
        return super().jobspec_create(args)

    def init_common(self, args):
        """Common initialization code for batch/alloc"""
        #  If number of slots not specified, then set it to node count
        #   if set, otherwise raise an error.
        if not args.nslots:
            if not args.nodes:
                raise ValueError("Number of slots to allocate must be specified")
            args.nslots = args.nodes
            args.exclusive = True

        if args.dump:
            args.broker_opts = args.broker_opts or []
            args.broker_opts.append("-Scontent.dump=" + args.dump)

        if args.add_brokers > 0:
            if not args.nodes:
                raise ValueError(
                    "--add-brokers may only be specified with -N, --nnodes"
                )
            nbrokers = args.add_brokers
            nnodes = args.nodes

            # Force update taskmap with extra ranks on nodeid 0:
            args.taskmap = f"manual:[[0,1,{1+nbrokers},1],[1,{nnodes-1},1,1]]"

            # Exclude the additional brokers via configuration. However,
            # don't throw away any ranks already excluded bythe user.
            # Note: raises an exception if user excluded by hostname (unlikely)
            exclude = IDset(args.conf.get("resource.exclude", default="")).set(
                1, nbrokers
            )
            args.conf.update(f'resource.exclude="{exclude}"')

        if args.time_limit is not None:
            try:
                limit = float(args.time_limit)
                args.time_limit = limit * 60
            except ValueError:
                # no conversion necessary
                pass

    def update_jobspec_common(self, args, jobspec):
        """Common jobspec update code for batch/alloc"""
        # If args.add_brokers is being used, update jobspec task count
        # to accurately reflect the updated task count.
        if args.add_brokers > 0:
            # Note: args.nodes required with add_brokers already checked above
            total_tasks = args.nodes + args.add_brokers

            # Overwrite task count with new total_tasks:
            jobspec.tasks[0]["count"] = {"total": total_tasks}

            # remove per-resource shell option which is no longer necessary:
            del jobspec.attributes["system"]["shell"]["options"]["per-resource"]
