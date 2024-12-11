##############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################
import glob
import textwrap
from abc import ABC
from os import getenv

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path


class CLIPluginValue:
    """Base class for dealing with values submitted through CLI. Used
    as an argparse custom type in the base CLI implementation.

    Args:
        value (str): the user input immediately following the plugin
            option submitted through the CLI. This value is split on
            the '=' delimiter and expected to follow the form PLUGIN[=VAL].
            Since the VAL is optional, self.value is True if it is not
            provided.
    """

    def __init__(self, value):
        temp = value.split("=")
        self.key = temp[0]
        try:
            self.value = temp[1]
        except IndexError:
            self.value = True

    def get_value(self):
        """Getter for returning the key and value"""
        return self.key, self.value

    def __repr__(self):
        return f"{self.key}={self.value}"


class CLIPluginNamespace:
    """Mutable base class for containing the value of plugins loaded in the
    CLIPluginRegistry.

    A plugin value is of the form PLUGIN[=VAL] where VAL is True if a VAL is
    not provided. If PLUGIN is loaded but not invoked by the user, that
    corresponding VAL is None.

    The utility of this class is that it allows individual plugins to call
    getattr(CLIPluginNamespace(), self.opt) with assurance that getattr will
    not raise an AttributeError because all loaded plugins will have an attribute,
    even if that attribute is None. This assurance is handled by the
    CLIPluginRegistry().
    """

    def __init__(self):
        pass

    def __repr__(self):
        rep = "CLIPluginNamespace("
        for attr in dir(self):
            if not attr.startswith("_"):
                rep += f"{attr}={getattr(self, attr)}"
        rep += ")"
        return rep


class CLIPlugin(ABC):  # pragma no cover
    """Base class for a CLI submission plugin

    A plugin should derive from this class and implement one or more
    base methods (described below)

    Attributes:
        opt (str): command-line key passed to -P/--plugin to activate the
            extension
        usage (str): --help message for the extension
        version (str): optional version for the plugin, preferred in format
            MAJOR.MINOR.PATCH
        prog (str): command-line subcommand for which the plugin is active,
            e.g. "submit", "run", "alloc", "batch", "bulksubmit"
    """

    def __init__(self, prog, version=None):
        self.prog = prog
        if prog.startswith("flux "):
            self.prog = prog[5:]
        self.options = {}
        self.version = version

    def add_options(self, registry):
        """Plugins implement this method to register options in the registry.

        Plugins can call:
        >>> registry.add_options("foo", usage="Enable foo.")

        to add new options to the registry.

        Args:
            registry (:py:obj:`CLIPluginRegistry`): registry of plugin options
                tracking both plugins and their values.
        """
        pass

    def preinit(self, args, plugin_args):
        """After parsing options, before jobspec is initialized

        Either here, or in validation, plugin extensions should check types and
        perform their own validation, since argparse will accept any string.

        Args:
            args (:py:obj:`Namespace`): Namespace result from
                :py:meth:`Argparse.ArgumentParser.parse_args()`.
            plugin_args (:py:obj:`CLIPluginNamespace`): Namespace of PLUGIN[=VAL]
                pairs returned by parsing the active plugin options. VALis
                None if PLUGIN was not invoked. VAL is True if the plugin
                was invoked without VAL.
        """
        pass

    def modify_jobspec(self, args, jobspec, plugin_args):
        """Allow plugin to modify jobspec

        This function is called after arguments have been parsed and jobspec
        has mostly been initialized.

        Args:
            args (:py:obj:`Namespace`): Namespace result from
                :py:meth:`Argparse.ArgumentParser.parse_args()`.
            jobspec (:obj:`flux.job.Jobspec`): instantiated jobspec object.
                This plugin can modify this object directly to enact
                changes in the current programs generated jobspec.
            plugin_args (:py:obj:`CLIPluginNamespace`): Namespace of PLUGIN[=VAL]
                pairs returned by parsing the active plugin options. VAL is
                None if PLUGIN was not invoked. VAL is True if the plugin
                was invoked without VAL.
        """
        pass

    def validate(self, jobspec):
        """Allow a plugin to validate jobspec

        This callback may be used by the cli itself or a job validator
        to validate the final jobspec.

        On an invalid jobspec, this callback should raise ValueError
        with a useful error message.

        Unlike with the ``plugin_args`` argument passed to ``preinit``
        and ``modify_jobspec``, a plugin may not assume that a given
        jobspec key will exist.

        Args:
            jobspec (:obj:`flux.job.Jobspec`): jobspec object to validate.
        """
        pass


class CLIPluginRegistry:
    """Flux CLI plugin registry class

    Base class that contains the registry of all plugins loaded in the
    instance. By default, plugins are loaded from ``{confdir}/cli/plugins``
    but this can be overridden by setting an environment variable,
    ``FLUX_CLI_PLUGINPATH``.
    """

    def __init__(self):
        self.plugins = []
        self.plugin_options = {}
        self.plugin_args = CLIPluginNamespace()
        etc = conf_builtin_get("confdir")
        if getenv("FLUX_CLI_PLUGINPATH"):
            self.plugindir = getenv("FLUX_CLI_PLUGINPATH")
        else:
            if etc is None:
                raise ValueError("failed to get builtin confdir")
            self.plugindir = f"{etc}/cli/plugins"

    def _add_plugins(self, module, program):
        entries = []
        for attr in dir(module):
            entries.append(getattr(module, attr))
        for entry in entries:
            # only process entries that are an instance of type (i.e. a class)
            # and are a subclass of CLIPlugin but not the base class:
            if (
                isinstance(entry, type)
                and issubclass(entry, CLIPlugin)
                and CLIPlugin != entry
            ):
                curr = entry(program)
                self.plugins.append(curr)
                curr.add_options(registry=self)

    def add_option(self, opt, usage):
        """Register an option string and optional --help message"""
        try:
            self.plugin_options[opt]
            raise ValueError(f"{opt} has already been registered")
        except KeyError:
            self.plugin_options[opt] = usage

    def parse_plugins(self, argparse_args):
        """Validate the invoked plugins and add the value to self.plugin_args"""
        for plugin in self.get_plugin_options():
            setattr(self.plugin_args, str(plugin), None)
        if argparse_args.plugin:
            for provided_arg in argparse_args.plugin:
                key, val = provided_arg.get_value()
                if key in self.get_plugin_options():
                    setattr(self.plugin_args, key, val)
                else:
                    raise ValueError(
                        f"Unsupported option provided to -P/--plugin: {key}"
                    )

    def load_plugins(self, program):
        """Load all cli plugins from the standard path"""
        for path in glob.glob(f"{self.plugindir}/*.py"):
            self._add_plugins(import_path(path), program)
        return self

    def get_plugin_options(self):
        """Return all plugin option keys from self.plugins"""
        return self.plugin_options.keys()

    def get_plugin_usages(self):
        """Return a string that has all self.plugin usage messages"""
        usages = None
        if self.plugins:
            usages = "Options provided by plugins:\n"
        for opt, usage in self.plugin_options.items():
            p_usage = textwrap.fill(usage, width=60).splitlines()
            usages += f"  {opt:<20}{p_usage[0]}\n"
            if len(p_usage) > 1:
                for line in p_usage[1:]:
                    usages += f"{' ' * 22}{line}\n"
        return usages

    def preinit(self, args):
        """Call all plugin ``preinit`` callbacks."""
        for plugin in self.plugins:
            plugin.preinit(args, self.plugin_args)

    def modify_jobspec(self, args, jobspec):
        """Call all plugin ``modify_jobspec`` callbacks"""
        for plugin in self.plugins:
            plugin.modify_jobspec(args, jobspec, self.plugin_args)

    def validate(self, jobspec):
        """Call any plugin validate callback"""
        for plugin in self.plugins:
            plugin.validate(jobspec)
