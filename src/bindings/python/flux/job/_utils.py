##############################################################
# Copyright 2026 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

# Utility functions and classes shared between flux.cli.base and
# flux.job (Jobspec.apply_options).

import fnmatch
import json
import os
import pathlib
import re
import resource
import signal
import sys
from collections import ChainMap
from itertools import chain
from string import Template
from urllib.parse import parse_qs, urlparse

try:
    import tomllib  # novermin
except ModuleNotFoundError:
    from flux.utils import tomli as tomllib

from flux import util
from flux.constraint.parser import ConstraintParser
from flux.util import dict_merge, get_treedict, set_treedict


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
        pass  # Fall through to raise ValueError
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

        # Emit deprecation warning for "after" scheme
        if name == "dependency" and self.scheme == "after":
            print(
                "dependency scheme 'after' is deprecated, use 'afterstart'",
                file=sys.stderr,
            )

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
    return [URIArg(uri, "dependency").entry for uri in uris]


def filter_dict(env, pattern, reverseMatch=True):
    """
    Filter out all keys that match "pattern" from dict 'env'

    Pattern is assumed to be a shell glob(7) pattern, unless it begins
    with '/', in which case the pattern is a regex.
    """
    if pattern.startswith("/"):
        pattern = pattern[1:].rstrip("/")
    else:
        pattern = fnmatch.translate(pattern)
    regex = re.compile(pattern)
    if reverseMatch:
        return {k: v for k, v in env.items() if not regex.match(k)}
    return {k: v for k, v in env.items() if regex.match(k)}


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
        rlimits[limit[7:].lower()] = soft
    if not rlimits:
        raise ValueError(f'No corresponding rlimit matching "{name}"')
    return rlimits


def list_split(opts):
    """
    Return a list by splitting each member of opts on ','
    """
    if isinstance(opts, str):
        opts = [opts]
    if opts:
        return list(chain.from_iterable(x.split(",") for x in opts))
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
            rlimits = filter_dict(rlimits, rule[1:].lower())
        else:
            name, *rest = rule.split("=", 1)
            if not rest:
                #  limit with no value pulls in current limit(s)
                rlimits.update(get_rlimits(name.lower()))
            else:
                if not hasattr(resource, f"RLIMIT_{name}".upper()):
                    raise ValueError(f'Invalid rlimit "{name}"')
                #  limit with value sets limit to that value
                if rest[0] in ("unlimited", "infinity", "inf"):
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
    env_expand = {}
    if environ is None:
        environ = dict(os.environ)
    if rules is None:
        return environ, env_expand
    for rule in rules:
        #
        #  If rule starts with '-' then the rest of the rule is a pattern
        #   which filters matching environment variables from the
        #   generated environment.
        #
        if rule.startswith("-"):
            environ = filter_dict(environ, rule[1:])
        #
        #  If rule starts with '^', then the result of the rule is a filename
        #   from which to read more rules.
        #
        elif rule.startswith("^"):
            filename = os.path.expanduser(rule[1:])
            try:
                with open(filename) as envfile:
                    lines = [line.strip() for line in envfile]
            except OSError as exc:
                raise ValueError(f"--env: {exc}") from None
            environ, envx = get_filtered_environment(lines, environ=environ)
            env_expand.update(envx)
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
            elif "{{" in rest[0]:
                #
                #  Mustache template which should be expanded by job shell.
                #  Place result in env_expand instead of environ:
                env_expand[var] = rest[0]
            else:
                #
                #  Template lookup: use jobspec environment first, fallback
                #   to current process environment using ChainMap:
                lookup = ChainMap(environ, os.environ)
                try:
                    environ[var] = Template(rest[0]).substitute(lookup)
                except ValueError:
                    raise ValueError(f"--env: Unable to substitute {rule}")
                except KeyError as ex:
                    raise ValueError(f"--env: Variable {ex} not found in {rule}")
    return environ, env_expand


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
        try:
            set_treedict(self.config, key, value)
        except TypeError as e:
            raise TypeError(f"failed to set {key} to {value}: {e}")
        return self

    def update_file(self, path, extension=".toml"):
        # Update from file in the filesystem
        try:
            loader = self.loaders[extension]
        except KeyError:
            raise ValueError(f"--conf: {path} must end in .toml or .json")
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

    def get(self, key, default=None):
        """
        Get a value from the current config using dotted key form
        """
        return get_treedict(self.config or {}, key, default=default)


def normalize_conf(conf):
    """Normalize *conf* to a dict for :meth:`from_nest_command`.

    Accepts a plain :class:`dict`, a :class:`~flux.job.BatchConfig`
    instance, or a string in any form accepted by
    :meth:`BatchConfig.update` (key=val, JSON, TOML, or a file path).
    Returns ``None`` when *conf* is ``None``.
    """
    if conf is None:
        return None
    if isinstance(conf, dict):
        return conf
    if isinstance(conf, BatchConfig):
        return conf.config
    return BatchConfig().update(str(conf)).config


def jobspec_add_file(jobspec, arg):
    """Process a single --add-file=ARG argument and attach it to jobspec."""
    perms = None
    #  Note: Replace any newline escaped by the shell with literal '\n'
    #  so that newline detection below works for file data passed on
    #  the command line:
    name, _, data = arg.replace("\\n", "\n").partition("=")
    if not data:
        # No '=' implies path-only argument (no multiline allowed)
        if "\n" in name:
            raise ValueError("--add-file: file name missing")
        data = name
        name = os.path.basename(data)
    else:
        # Check if name specifies permissions after ':'
        tmpname, _, permstr = name.partition(":")
        try:
            perms = int(permstr, base=8)
            name = tmpname
        except ValueError:
            # assume ':' was part of name
            pass
    try:
        jobspec.add_file(name, data, perms=perms)
    except (TypeError, ValueError, OSError) as exc:
        raise ValueError(f"--add-file={arg}: {exc}") from None


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
            # val was not a JSON string, keep as is
            pass
    return key, val
