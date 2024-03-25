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
import fnmatch
import json
import logging
import os
import pathlib
import re
import resource
import signal
import sys
from collections import ChainMap
from itertools import chain
from os.path import basename
from string import Template
from urllib.parse import parse_qs, urlparse

try:
    import tomllib  # novermin
except ModuleNotFoundError:
    from flux.utils import tomli as tomllib

import flux
from flux import debugged, job, util
from flux.constraint.parser import ConstraintParser, ConstraintSyntaxError
from flux.idset import IDset
from flux.job import JobspecV1, JobWatcher
from flux.progress import ProgressBar
from flux.util import dict_merge, set_treedict

LOGGER = logging.getLogger("flux")


def decode_signal(val):
    """
    Decode a signal as string or number
    A string can be of the form 'SIGUSR1' or just 'USR1'
    """
    if isinstance(val, int):
        return val
    try:
        return int(val)
    except ValueError:
        pass  # Fall back to signal name
    try:
        return getattr(signal, val)
    except AttributeError:
        pass  # Fall back SIG{name}
    try:
        return getattr(signal, f"SIG{val}")
    except AttributeError:
        pass
    raise ValueError(f"signal '{val}' is invalid")


def decode_duration(val):
    """
    Decode a duration as a number or string in FSD
    """
    if isinstance(val, (float, int)):
        return val
    try:
        return float(val)
    except ValueError:
        pass  # Fall back to fsd
    return util.parse_fsd(val)


def parse_signal_option(arg):
    """
    Parse the --signal= option argument of the form SIG@TIME, where
    both signal and time are optional.

    Returns a dict with signal and timeleft members
    """
    signo = signal.SIGUSR1
    tleft = 60
    if arg is not None:
        sig, _, time = arg.partition("@")
        if time:
            tleft = time
        if sig:
            signo = sig
    try:
        signum = decode_signal(signo)
        if signum <= 0:
            raise ValueError("signal must be > 0")
    except ValueError as exc:
        raise ValueError(f"--signal={arg}: {exc}") from None

    try:
        timeleft = decode_duration(tleft)
    except ValueError as exc:
        raise ValueError(f"--signal={arg}: {exc}") from None

    return {"signum": signum, "timeleft": timeleft}


class MiniConstraintParser(ConstraintParser):
    operator_map = {
        None: "properties",
        "host": "hostlist",
        "hosts": "hostlist",
        "rank": "ranks",
    }
    split_values = {"properties": ","}
    combined_terms = {"properties"}


class URIArg:
    """Convenience class for handling dependencies

    Splits a dependency URI into fields and returns an RFC 26 dependency
    entry via the entry attribute.
    """

    def __init__(self, uri, name):
        # append `:` if missing in uri so that a plain string is treated as
        # a scheme with no path.
        if ":" not in uri:
            uri += ":"

        # replace first ':' with ':FXX' to work around urlparse refusal
        # to treat integer only path as a scheme:path.
        self.uri = urlparse(uri.replace(":", ":FXX", 1))

        if not self.uri.scheme or not self.uri.path:
            raise ValueError(f'Invalid {name} URI "{uri}"')

        self.path = self.uri.path.replace("FXX", "", 1)
        self.scheme = self.uri.scheme

    @staticmethod
    def _try_number(value):
        """Convert value to an int or a float if possible"""
        for _type in (int, float):
            try:
                return _type(value)
            except ValueError:
                continue
        return value

    @property
    def entry(self):
        entry = {
            "scheme": self.scheme,
            "value": self.path,
        }
        if self.uri.query:
            for key, val in parse_qs(self.uri.query).items():
                #  val is always a list, but convert to single value
                #   if it only contains a single item:
                if len(val) > 1:
                    entry[key] = [self._try_number(x) for x in val]
                else:
                    entry[key] = self._try_number(val[0])
        return entry


def dependency_array_create(uris):
    dependencies = []
    for uri in uris:
        dependencies.append(URIArg(uri, "dependency").entry)
    return dependencies


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


def get_rlimits(name="*"):
    """
    Return set of rlimits matching `name` in a dict
    """
    rlimits = {}
    pattern = f"RLIMIT_{name}".upper()
    for limit in fnmatch.filter(resource.__dict__.keys(), pattern):
        soft, hard = resource.getrlimit(getattr(resource, limit))
        #  Remove RLIMIT_ prefix and lowercase the result for compatibility
        #  with rlimit shell plugin:
        rlimits[limit[7::].lower()] = soft
    if not rlimits:
        raise ValueError(f'No corresponding rlimit matching "{name}"')
    return rlimits


def list_split(opts):
    """
    Return a list by splitting each member of opts on ','
    """
    if opts:
        x = chain.from_iterable([x.split(",") for x in opts])
        return list(x)
    return []


def get_filtered_rlimits(user_rules=None):
    """
    Get a filtered set of rlimits based on user rules and defaults
    """
    #  We start with the entire set of available rlimits and then apply
    #  rules to remove or override them. Therefore, the set of default
    #  rules excludes limits *not* to propagate by default.
    rules = [
        "-memlock",
        "-ofile",
        "-msgqueue",
        "-nice",
        "-rtprio",
        "-rttime",
        "-sigpending",
    ]

    if user_rules:
        rules.extend(list_split(user_rules))

    rlimits = get_rlimits()
    for rule in rules:
        if rule.startswith("-"):
            # -name removes RLIMIT_{pattern} from propagated rlimits
            rlimits = filter_dict(rlimits, rule[1::].lower())
        else:
            name, *rest = rule.split("=", 1)
            if not rest:
                #  limit with no value pulls in current limit(s)
                limits = get_rlimits(name.lower())
                for key, value in limits.items():
                    rlimits[key] = value
            else:
                if not hasattr(resource, f"RLIMIT_{name}".upper()):
                    raise ValueError(f'Invalid rlimit "{name}"')
                #  limit with value sets limit to that value
                if rest[0] in ["unlimited", "infinity", "inf"]:
                    value = resource.RLIM_INFINITY
                else:
                    try:
                        value = int(rest[0])
                    except ValueError:
                        raise ValueError(f"Invalid value in {name}={rest[0]}")
                rlimits[name.lower()] = value
    return rlimits


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
                except ValueError:
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


class BatchConfig:
    """Convenience class for handling a --conf=[FILE|KEY=VAL] option

    Iteratively build a "config" dict from successive updates.
    """

    loaders = {".toml": tomllib.load, ".json": json.load}

    def __init__(self):
        self.config = None

    def update_string(self, value):
        # Update config with JSON or TOML string
        try:
            conf = json.loads(value)
        except json.decoder.JSONDecodeError:
            # Try TOML
            try:
                conf = tomllib.loads(value)
            except tomllib.TOMLDecodeError:
                raise ValueError(
                    "--conf: failed to parse multiline as TOML or JSON"
                ) from None
        self.config = dict_merge(self.config, conf)
        return self

    def update_keyval(self, keyval):
        # dotted key (e.g. resource.noverify=true)
        key, _, value = keyval.partition("=")
        try:
            value = json.loads(value)
        except json.decoder.JSONDecodeError:
            value = str(value)
        set_treedict(self.config, key, value)
        return self

    def update_file(self, path, extension=".toml"):
        # Update from file in the filesystem
        try:
            loader = self.loaders[extension]
        except KeyError:
            raise ValueError("--conf: {path} must end in .toml or .json")
        try:
            with open(path, "rb") as fp:
                conf = loader(fp)
        except OSError as exc:
            raise ValueError(f"--conf: {exc}") from None
        except (json.decoder.JSONDecodeError, tomllib.TOMLDecodeError) as exc:
            raise ValueError(f"--conf: parse error: {path}: {exc}") from None
        self.config = dict_merge(self.config, conf)
        return self

    def _find_config(self, name):
        # Find a named config as either TOML or JSON in XDG search path
        for path in util.xdg_searchpath(subdir="config"):
            # Take the first matching filename preferring TOML:
            for ext in (".toml", ".json"):
                filename = f"{path}/{name}{ext}"
                if os.path.exists(filename):
                    return filename, self.loaders[ext]
        return None, None

    def update_named_config(self, name):
        # Update from a named configuration file in a standard path or paths.
        filename, loader = self._find_config(name)
        if filename is not None:
            try:
                with open(filename, "rb") as fp:
                    self.config = dict_merge(self.config, loader(fp))
                    return self
            except (
                OSError,
                tomllib.TOMLDecodeError,
                json.decoder.JSONDecodeError,
            ) as exc:
                raise ValueError(f"--conf={name}: {filename}: {exc}") from None
        raise ValueError(f"--conf: named config '{name}' not found")

    def update(self, value):
        """
        Update current config with value using the following rules:
        - If value contains one or more newlines, parse it as a JSON or
          TOML string.
        - Otherwise, if value contains an ``=``, then parse it as a dotted
          key and value, e.g. ``resource.noverify=true``. The value (part
          after the ``=``) will be parsed as JSON.
        - Otherwise, if value ends in ``.toml`` or ``.json`` treat value as
          a path and attempt to parse contents of file as TOML or JSON.
        - Otherwise, read a named config from a standard config search path.

        Configuration can be updated iteratively. The end result is available
        in the ``config`` attribute.
        """
        if self.config is None:
            self.config = {}
        if "\n" in value:
            return self.update_string(value)
        if "=" in value:
            return self.update_keyval(value)
        extension = pathlib.Path(value).suffix
        if extension in (".toml", ".json"):
            return self.update_file(value, extension)
        return self.update_named_config(value)


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


def parse_jobspec_keyval(label, keyval):
    """Parse a key[=value] option as used with --setopt and --setattr

    Supports ^key=filename to load JSON object from a file
    """
    # Split into key, val with a default of 1 if no val given:
    key, val = (keyval.split("=", 1) + [1])[:2]

    # Support key prefix of ^ to load value from a file
    if key.startswith("^"):
        key = key.lstrip("^")
        with open(val) as filep:
            try:
                val = json.load(filep)
            except (json.JSONDecodeError, TypeError) as exc:
                raise ValueError(f"{label}: {val}: {exc}") from exc
    else:
        try:
            val = json.loads(val)
        except (json.JSONDecodeError, TypeError):
            pass
    return key, val


class MiniCmd:
    """
    MiniCmd is the base class for all flux submission subcommands
    """

    def __init__(self, prog, usage=None, description=None, exclude_io=False):
        self.flux_handle = None
        self.exitcode = 0
        self.progress = None
        self.watcher = None
        self.parser = self.create_parser(prog, usage, description, exclude_io)

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
            + "specified, then the basename of PATH will be used. (multiple "
            + "use OK)",
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

    # pylint: disable=too-many-branches,too-many-statements
    def jobspec_create(self, args):
        """
        Create a jobspec from args and return it to caller
        """
        jobspec = self.init_jobspec(args)
        jobspec.environment = get_filtered_environment(args.env)
        jobspec.cwd = args.cwd if args.cwd is not None else os.getcwd()
        rlimits = get_filtered_rlimits(args.rlimit)
        if rlimits:
            jobspec.setattr_shell_option("rlimit", rlimits)
        if args.signal:
            entry = parse_signal_option(args.signal)
            jobspec.setattr_shell_option("signal", entry)

        # --taskmap is only defined for run/submit, but we check
        # for it in the base jobspec_create() for convenience
        if hasattr(args, "taskmap") and args.taskmap is not None:
            jobspec.setattr_shell_option(
                "taskmap", URIArg(args.taskmap, "taskmap").entry
            )

        if args.dependency is not None:
            jobspec.setattr(
                "system.dependencies", dependency_array_create(args.dependency)
            )
        if args.requires is not None:
            constraint = " ".join(args.requires)
            try:
                jobspec.setattr(
                    "system.constraints", MiniConstraintParser().parse(constraint)
                )
            except ConstraintSyntaxError as exc:
                raise ValueError(f"--requires='{constraint}': {exc}")
        if args.time_limit is not None:
            #  With no units, time_limit is in minutes, but jobspec.duration
            #  takes seconds or FSD by default, so convert here if necessary.
            try:
                limit = float(args.time_limit)
                args.time_limit = limit * 60
            except ValueError:
                pass
            jobspec.duration = args.time_limit

        if args.job_name is not None:
            jobspec.setattr("system.job.name", args.job_name)

        if args.input is not None:
            #  Note: temporary stopgap until per-task input is supported
            #  by the job shell:
            #  Check if --input specified an IDset. If not, then assume
            #  a file, otherwise, do not modify jobspec, input will be
            #  handled by `flux job attach`.
            try:
                IDset(args.input)
            except (ValueError, OSError):
                jobspec.stdin = args.input

        if args.output is not None and args.output not in ["none", "kvs"]:
            jobspec.stdout = args.output
            if args.label_io:
                jobspec.setattr_shell_option("output.stdout.label", True)

        if args.error is not None:
            jobspec.stderr = args.error
            if args.label_io:
                jobspec.setattr_shell_option("output.stderr.label", True)

        if args.unbuffered:
            #  For output, set the buffer.type to none and reduce the configured
            #  event batch-timeout to something very small.
            jobspec.setattr_shell_option("output.stdout.buffer.type", "none")
            jobspec.setattr_shell_option("output.stderr.buffer.type", "none")
            jobspec.setattr_shell_option("output.batch-timeout", 0.05)

        if args.setopt is not None:
            for keyval in args.setopt:
                key, val = parse_jobspec_keyval("--setopt", keyval)
                jobspec.setattr_shell_option(key, val)

        if args.debug_emulate:
            debugged.set_mpir_being_debugged(1)

        if debugged.get_mpir_being_debugged() == 1:
            # if stop-tasks-in-exec is present, overwrite
            jobspec.setattr_shell_option("stop-tasks-in-exec", json.loads("1"))

        if args.queue is not None:
            jobspec.setattr("system.queue", args.queue)

        if args.setattr is not None:
            for keyval in args.setattr:
                key, val = parse_jobspec_keyval("--setattr", keyval)

                #  If key does not explicitly start with ".", "system."
                #   or "user.", then "system." is implied. This is a
                #   meant to be a usability enhancement since almost all
                #   uses of --setattr will target attributes.system.
                #
                if not key.startswith((".", "user.", "system.")):
                    key = "system." + key

                #  Allow key to begin with "." which simply forces the key
                #   to start at the top level (since .system is not applied
                #   due to above conditional)
                jobspec.setattr(key.lstrip("."), val)

        if args.add_file is not None:
            for arg in args.add_file:
                name, _, data = arg.partition("=")
                if not data:
                    # No '=' implies path-only argument (no multiline allowed)
                    if "\n" in name:
                        raise ValueError("--add-file: file name missing")
                    data = name
                    name = basename(data)
                try:
                    jobspec.add_file(name, data)
                except (TypeError, ValueError, OSError) as exc:
                    raise ValueError(f"--add-file={arg}: {exc}") from None

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
            self.flux_handle.reactor_decref()

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


def add_batch_alloc_args(parser):
    """
    Add "batch"-specific resource allocation arguments to parser object
    which deal in slots instead of tasks.
    """
    parser.add_argument(
        "--conf",
        metavar="CONF",
        default=BatchConfig(),
        action=ConfAction,
        help="Set configuration for a child Flux instance. CONF may be a "
        + "multiline string in JSON or TOML, a configuration key=value, a "
        + "path to a JSON or TOML file, or a configuration loaded by name "
        + "from a standard search path. This option may specified multiple "
        + "times, in which case the config is iteratively updated.",
    )
    parser.add_argument(
        "--broker-opts",
        metavar="OPTS",
        default=None,
        action="append",
        help="Pass options to flux brokers",
    )
    parser.add_argument(
        "--dump",
        nargs="?",
        const="flux-{{jobid}}-dump.tgz",
        metavar="FILE",
        help="Archive KVS on exit",
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
    parser.add_argument(
        "-x",
        "--exclusive",
        action="store_true",
        help="With -N, --nodes, allocate nodes exclusively",
    )
