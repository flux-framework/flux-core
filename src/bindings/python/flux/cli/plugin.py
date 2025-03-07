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

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path


class CLIPluginValue:
    def __init__(self, value):
        temp = value.split("=")
        self.key = temp[0]
        try:
            self.value = temp[1]
        except IndexError:
            self.value = ""

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

    def __init__(self, prog):
        self.opt = None
        self.usage = None
        self.help = None
        self.version = None
        self.prog = prog
        if prog.startswith("flux "):
            self.prog = prog[5:]

    def get_plugin_name(self):
        """Return the KEY to invoke the plugin"""
        return self.opt

    def get_help_message(self):
        """Return the message to print with --help"""
        return self.help

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
    """Flux CLI plugin registry helper class"""

    def __init__(self):
        self.plugins = []
        etc = conf_builtin_get("confdir")
        if etc is None:
            raise ValueError("failed to get builtin confdir")
        if getenv("FLUX_CLI_PLUGINPATH"):
            self.plugindir = getenv("FLUX_CLI_PLUGINPATH")
        else:
            self.plugindir = f"{etc}/cli/plugins"

    def _add_plugins(self, module, program):
        entries = [
            getattr(module, attr) for attr in dir(module) if not attr.startswith("_")
        ]
        ## TODO: as we get each entry, make sure it is NOT speciifically the CLIPlugin base class
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

    def add_plugin_option(self, parser):
        """Add the -P option and list of available keys"""
        parser.add_argument(
            "-P",
            "--plugin",
            type=CLIPluginValue,
            action="append",
            help=f"{self.get_plugin_help_messages()}",
        )

    def get_plugin_options(self):
        """Return all plugin option keys from self.plugins"""
        opts = []
        for plugin in self.plugins[1:]:
            opts.append(str(plugin.get_plugin_name()))
        return opts

    def get_plugin_help_messages(self):
        """Return a string that has all self.plugin help messages"""
        ## TODO: Utilize the HelpFormatter class in argparse to make
        ##  these messages better
        help = ""
        for plugin in self.plugins[1:]:
            help += str(plugin.get_plugin_name())
            help += str(plugin.get_help_message())
            help += "\n"
        return help

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
