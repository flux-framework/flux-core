##############################################################
# Copyright 2024 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
##############################################################
from os import getenv
import glob
from abc import ABC
import textwrap

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path


class CLIPluginValue:
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


class CLIPlugin(ABC):  # pragma no cover
    """Base class for a CLI submission plugin

    A plugin should derive from this class and implement one or more
    base methods (described below)

    Attributes:
        opt (str): command-line key passed to -P/--plugin to activate the
            extension
        usage (str): short --help message for the extension
        help (str): long --help message for the extension
        version (str): optional version for the plugin, preferred in format
            MAJOR.MINOR.PATCH
        prog (str): command-line subcommand for which the plugin is active,
            e.g. "submit", "run", "alloc", "batch", "bulksubmit"
    """

    def __init__(self, prog, opt, usage, version=None):
        self.prog = prog
        if prog.startswith("flux "):
            self.prog = prog[5:]
        self.opt = opt
        self.usage = usage
        self.version = version

    def preinit(self, args, values):
        """After parsing options, before jobspec is initialized

        Either here, or in validation, plugin extensions should check types and
        perform their own validation, since argparse will accept any string.

        Args:
            args (:py:obj:`Namespace`): Namespace result from
                :py:meth:`Argparse.ArgumentParser.parse_args()`.
            values (:obj:`Dict`): Dictionary of KEY[=VALUE] pairs returned
                by parsing the arguments provided to the -P/--plugin
                option. KEY is None if plugin was not invoked. VALUE is the
                empty string if the plugin was invoked without VALUE.
        """
        pass

    def modify_jobspec(self, args, jobspec, values):
        """Allow plugin to modify jobspec

        This function is called after arguments have been parsed and jobspec
        has mostly been initialized.

        Args:
            args (:py:obj:`Namespace`): Namespace result from
                :py:meth:`Argparse.ArgumentParser.parse_args()`.
            jobspec (:obj:`flux.job.Jobspec`): instantiated jobspec object.
                This plugin can modify this object directly to enact
                changes in the current programs generated jobspec.
            values (:obj:`Dict`): Dictionary of KEY[=VALUE] pairs returned
                by parsing the arguments provided to the -P/--plugin
                option. KEY is None if plugin was not invoked. VALUE is the
                empty string if the plugin was invoked without VALUE.
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
    """Flux CLI plugin registry class

    Base class that contains the registry of all plugins loaded in the
    instance. By default, plugins are loaded from ``{confdir}/cli/plugins``
    but this can be overriden by setting an environment variable,
    ``FLUX_CLI_PLUGINPATH``.
    """

    def __init__(self):
        self.plugins = []
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
            if not attr.startswith("_"):
                if not attr.startswith("CLIPlugin"):  ## ignore the base class
                    entries.append(getattr(module, attr))
        for entry in entries:
            # only process entries that are an instance of type (i.e. a class)
            # and are a subclass of CLIPlugin:
            if isinstance(entry, type) and issubclass(entry, CLIPlugin):
                self.plugins.append(entry(program))

    def load_plugins(self, program):
        """Load all cli plugins from the standard path"""
        for path in glob.glob(f"{self.plugindir}/*.py"):
            self._add_plugins(import_path(path), program)
        return self

    def get_plugin_options(self):
        """Return all plugin option keys from self.plugins"""
        opts = [plugin.opt for plugin in self.plugins]
        return opts

    def get_plugin_usages(self):
        """Return a string that has all self.plugin usage messages"""
        usage = None
        if self.plugins:
            usage = "Options provided by plugins:\n"
        for plugin in self.plugins:
            p_usage = textwrap.fill(plugin.usage, width=60).splitlines()
            usage += f"  {plugin.opt:<20}{p_usage[0]}\n"
            if len(p_usage) > 1:
                for line in p_usage[1:]:
                    usage += f"{' ' * 22}{line}\n"
        return usage

    def preinit(self, args, values):
        """Call all plugin ``preinit`` callbacks"""
        for plugin in self.plugins:
            plugin.preinit(args, values)

    def modify_jobspec(self, args, jobspec, value):
        """Call all plugin ``modify_jobspec`` callbacks"""
        for plugin in self.plugins:
            plugin.modify_jobspec(args, jobspec, value)

    def validate(self, jobspec):
        """Call any plugin validate callback"""
        for plugin in self.plugins:
            plugin.validate(jobspec)
