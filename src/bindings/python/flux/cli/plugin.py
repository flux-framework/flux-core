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
import os
import sys
import termios
from abc import ABC
from pydoc import ttypager

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path, import_plugins


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
        self.path = None
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

    default_plugins = ["shape"]
    plugin_namespace = "flux.cli.plugins"

    def __init__(self, prog):
        self.prog = prog
        self.plugins = []
        self.plugindirs = self._get_searchpath()
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

    def _get_searchpath(self):
        """
        Return list of dirs to search for CLI plugins.

        If ``FLUX_CLI_PLUGINPATH_OVERRIDE`` is set to "", return empty list
        (only namespaced plugins will be loaded).

        If ``FLUX_CLI_PLUGINPATH_OVERRIDE`` is set to a path, return only those
        paths (system defaults are suppressed entirely).

        Otherwise, prepend any paths from ``FLUX_CLI_PLUGINPATH`` to the
        system default search path.
        """
        sysdir = conf_builtin_get("confdir")
        builtindir = conf_builtin_get("libexecdir")
        builtin_paths = [f"{sysdir}/cli/plugins", f"{builtindir}/cli/plugins"]

        if "FLUX_CLI_PLUGINPATH_OVERRIDE" in os.environ:
            raw = os.environ["FLUX_CLI_PLUGINPATH_OVERRIDE"]
            return [s for s in raw.split(":") if s and not s.isspace()]

        paths = list(builtin_paths)
        if "FLUX_CLI_PLUGINPATH" in os.environ:
            extra = [
                s
                for s in os.environ["FLUX_CLI_PLUGINPATH"].split(":")
                if s and not s.isspace()
            ]
            paths = extra + paths

        return paths

    def _add_plugins_from_module(self, module, program):
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
                plugin = entry(program)
                plugin.path = module.__file__
                self.plugins.append(plugin)

    def _add_plugins(self, path, program):
        module = import_path(path)
        self._add_plugins_from_module(module, program)

    def print_plugins(self):
        """Print all of the plugins loaded by _load_plugins."""
        print("Options provided by plugins:")
        if self.plugindirs:
            print(f"  Search path: {':'.join(self.plugindirs)}")
            print(f"  (plus {self.plugin_namespace} namespace)\n")
        else:
            print(f"  Searched only {self.plugin_namespace} namespace\n")
        for plugin in self.plugins:
            print(f"{type(plugin).__name__} loaded from {plugin.path}")
            for option in plugin.options:
                print(f"  {option.name:<20}  {option.kwargs['help']}")
            print()

    def _load_plugins(self, program):
        """Load all cli plugins from the standard path"""
        # First load filesystem plugins from search path
        for plugindir in self.plugindirs:
            for path in glob.glob(f"{plugindir}/*.py"):
                self._add_plugins(path, program)
        # Then load builtin plugins
        packaged = import_plugins(self.plugin_namespace)
        for name in self.default_plugins:
            if name in packaged:
                self._add_plugins_from_module(packaged[name], program)
        # keep a dictionary of options:plugin so that conflicts can be checked
        option_dests = {}
        # because the plugin list can change due to conflicting options, iterate
        # over a copy, not the list itself
        for plugin in self.plugins[:]:
            if any(opt.dest in option_dests for opt in plugin.options):
                # remove the current plugin if any of the options conflict (since
                # it would have come after the 'primary' plugin observed in the
                # PATH)
                self.plugins.remove(plugin)
            else:
                for opt in plugin.options:
                    option_dests[opt.dest] = opt
        # finalize all of this by passing a list of argparse arguments (options)
        # to the registry
        self.options = list(option_dests.values())
        return self

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
