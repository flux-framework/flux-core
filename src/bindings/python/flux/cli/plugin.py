##############################################################
# Copyright 2025 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################

import glob
import inspect
import sys
import termios
from abc import ABC
from os import getenv
from pydoc import ttypager

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path


class PluginArgsProxy:
    """Per-plugin proxy for the args namespace passed to plugin callbacks.

    Translates attribute access using an unprefixed dest name (e.g.
    ``my_option``) to the current prefixed dest (e.g. ``site_my_option``),
    so that existing plugin callback code continues to work after the dest
    was changed to include the prefix.

    Attributes not in the alias map (i.e. from builtin options such as ``conf``
    and ``env``) pass through to the underlying namespace unchanged.
    """

    __slots__ = ("_ns", "_alias")

    def __init__(self, args, alias):
        # Use object.__setattr__ to write our own slots directly, bypassing
        # the custom __setattr__ below. O/w, Using self._ns = ... would call
        # __setattr__, which could read _alias before it has been assigned.
        object.__setattr__(self, "_ns", args)
        object.__setattr__(self, "_alias", alias)

    def __getattr__(self, name):
        # Use object.__getattribute__ to read our own slots directly,
        # bypassing any subclass overrides and making clear we are accessing
        # proxy internals rather than forwarding to the wrapped namespace.
        alias = object.__getattribute__(self, "_alias")
        ns = object.__getattribute__(self, "_ns")
        return getattr(ns, alias.get(name, name))

    def __setattr__(self, name, value):
        # Same reasoning as __getattr__: read our own slots directly.
        alias = object.__getattribute__(self, "_alias")
        ns = object.__getattribute__(self, "_ns")
        setattr(ns, alias.get(name, name), value)


class CLIPluginOption:
    """Wrap Argparse's add_argument method with a class"""

    def __init__(self, name, prefix, **kwargs):
        if not name.startswith("--"):
            raise ValueError("Plugins must register only long options.")
        if prefix:
            ## only replace the first occurrence of --
            self.name = name.replace("--", f"--{prefix}-", 1)
        else:
            self.name = name
        if "dest" not in kwargs:
            # Derive dest from the prefixed CLI flag so it matches what
            # callers see in --help output and no inadvertent conflicts
            # with builtin options dest occur. Store the unprefixed form
            # so PluginArgsProxy can alias transparently.
            self._unprefixed_dest = name[2:].replace("-", "_")
            kwargs["dest"] = self.name[2:].replace("-", "_")
        else:
            # Explicit dest= means the plugin author chose the name
            # deliberately; no backward-compatibility alias is needed.
            self._unprefixed_dest = None
        self.kwargs = kwargs

    @property
    def dest(self):
        return self.kwargs["dest"]


class CLIPlugin(ABC):  # pragma no cover
    """Base class for a CLI submission plugin

    A plugin should derive from this class and implement one or more
    base methods (described below)

    Attributes:
        prog (str): command-line subcommand for which the plugin is active,
            e.g. "submit", "run", "alloc", "batch", "bulksubmit"
        prefix (str): By default, ``--ex-`` is added as a prefix to ensure
            plugin-provided options are namespaced. The prefix may be
            overridden with a site or project name.

    """

    def __init__(self, prog, prefix="ex", version=None):
        self.prog = prog
        if prog.startswith("flux "):
            self.prog = prog[5:]
        self.prefix = prefix
        self.version = version
        self.options = []

    def help(self, option):
        """Print a help message for this plugin and option ``option``

        By default, prints the plugin's docstring to stdout.
        Plugins may override this behavior by overriding this method.
        """
        docstring = inspect.getdoc(self)
        if docstring is None or docstring == inspect.getdoc(CLIPlugin):
            print(
                f"No extra documentation for option `{option.name}` found.",
                file=sys.stderr,
            )
            return
        print(f"\nDocumentation for plugin providing option `{option.name}`:\n")
        try:
            ttypager(docstring)
        except termios.error:
            sys.stdout.write(docstring + "\n")

    def add_option(self, name, **kwargs):
        """Allow plugin to register options in its dictionary

        Args:
            name (str): Long option string being added (must begin with ``--``).

        Other keyword arguments, except ``dest=`` are passed along to
        ``ArgumentParser.add_argument`` unchanged. When ``dest=`` is not
        given it is derived from the full prefixed CLI flag name
        (e.g. ``--site-my-option`` = ``site_my_option``) so that the
        kwarg name matches what callers see in ``--help`` output. An explicit
        ``dest=`` is used as-is and no backward-compatibility alias is
        constructed. As a convenience, within plugin callbacks accesses to
        ``args.my_option`` are automatically proxied for plugins to
        ``args.<prefix>_my_option``.

        Plugins use:
        >>> self.add_option("--longopt", action="store_true", help="Help.")

        to add new options to its options list. If the option should only
        be active for certain subcommands, then the option should be added
        conditionally based on :py:attr:`self.prog`.

        The options list is queried by the command (:py:attr:`self.prog`),
        which loads the registered options with argparse.
        """
        self.options.append(CLIPluginOption(name, prefix=self.prefix, **kwargs))

    def preinit(self, args):
        """After parsing options, before jobspec is initialized"""
        pass

    def modify_jobspec(self, args, jobspec):
        """Allow plugin to modify jobspec

        This function is called after arguments have been parsed and jobspec
        has mostly been initialized.

        Args:
            args (:py:obj:`Namespace`): Namespace result from
                :py:meth:`Argparse.ArgumentParser.parse_args()`.
            jobspec (:obj:`flux.job.Jobspec`): instantiated jobspec object.
                This plugin can modify this object directly to enact
                changes in the current programs generated jobspec.
        """
        pass

    def validate(self, jobspec):
        """Allow a plugin to validate jobspec

        This callback may be used by the cli itself or a job validator
        to validate the final jobspec.

        On an invalid jobspec, this callback should raise ValueError
        with a useful error message.

        Args:
            jobspec (:obj:`flux.job.Jobspec`): jobspec object to validate.
        """
        pass


class CLIPluginRegistry:
    """Flux CLI plugin registry helper class"""

    def __init__(self, prog):
        self.prog = prog
        self.plugins = []
        etc = conf_builtin_get("confdir")
        if getenv("FLUX_CLI_PLUGINPATH"):
            self.plugindir = getenv("FLUX_CLI_PLUGINPATH")
        else:
            if etc is None:
                raise ValueError("failed to get builtin confdir")
            self.plugindir = f"{etc}/cli/plugins"
        self._load_plugins(self.prog)

    def print_help(self, name):
        """
        Print docstring for plugin associated with option ``name`` and exit

        Raises:
            ValueError: No help found for ``name``
        """
        for plugin in self.plugins:
            for option in plugin.options:
                # match name to option with or without leading `--`:
                if name in (option.name, option.name.lstrip("-")):
                    plugin.help(option)
                    sys.exit(0)
        raise ValueError(f"--help: no such option {name}")

    def _add_plugins(self, module, program):
        entries = [
            getattr(module, attr) for attr in dir(module) if not attr.startswith("_")
        ]
        for entry in entries:
            # only process entries that are an instance of type (i.e. a class)
            # and are a subclass of CLIPlugin, but not the base class:
            if (
                isinstance(entry, type)
                and issubclass(entry, CLIPlugin)
                and entry != CLIPlugin
            ):
                self.plugins.append(entry(program))

    def _load_plugins(self, program):
        """Load all cli plugins from the standard path"""
        for path in glob.glob(f"{self.plugindir}/*.py"):
            self._add_plugins(import_path(path), program)
        option_dests = {}
        self.options = []
        for plugin in self.plugins:
            for option in plugin.options:
                if option.dest in option_dests:
                    raise ValueError(
                        f"{option.name} conflicts with another option (or its `dest`)"
                    )
                else:
                    self.options.append(option)
                    ## keep a temporary list of "dests" to ensure no conflicts
                    option_dests[option.dest] = 1
        return self  ## possibly unnecessary now

    def _make_alias(self, plugin):
        """Return {unprefixed_dest: prefixed_dest} alias map for *plugin*.

        Only options whose dest was auto-derived (no explicit ``dest=``)
        and whose unprefixed and prefixed forms differ get an entry.
        """
        return {
            opt._unprefixed_dest: opt.dest
            for opt in plugin.options
            if opt._unprefixed_dest is not None and opt._unprefixed_dest != opt.dest
        }

    def preinit(self, args):
        """Call all plugin ``preinit`` callbacks"""
        for plugin in self.plugins:
            plugin.preinit(PluginArgsProxy(args, self._make_alias(plugin)))

    def modify_jobspec(self, args, jobspec):
        """Call all plugin ``modify_jobspec`` callbacks"""
        for plugin in self.plugins:
            plugin.modify_jobspec(
                PluginArgsProxy(args, self._make_alias(plugin)), jobspec
            )

    def validate(self, jobspec):
        """Call any plugin validate callback"""
        for plugin in self.plugins:
            plugin.validate(jobspec)


# vi: ts=4 sw=4 expandtab
