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
from abc import ABC
from os import getenv

from flux.conf_builtin import conf_builtin_get
from flux.importer import import_path


class CLIPluginArgumentGroup:
    """Wrap the argparse argument group for plugin-added options

    The purpose of this class is to ensure limited ability for plugins
    to add options to submission cli commands, to keep those options in
    separate group in the default `--help` output, as well as optionally
    add a prefix to all plugin-added options (default: `--ex-{name}`)
    """

    def __init__(self, parser, prefix="--ex-"):
        self.group = parser.add_argument_group("Options provided by plugins")
        self.prefix = prefix

    def add_argument(self, option_string, **kwargs):
        """Wrapped add_argument() call for the CLI plugins argument group

        Args:
            option_string (str): Long option string being added (must begin
                with ``--``.

        Other keyword arguments, except ``dest=`` are passed along to
        Argparse.add_argument.
        """
        if not option_string.startswith("--"):
            raise ValueError("Plugins must only register long options")
        if self.prefix:
            if "dest" not in kwargs:
                kwargs["dest"] = option_string[2:].replace("-", "_")
            option_string = option_string.replace("--", self.prefix)
        return self.group.add_argument(option_string, **kwargs)


class CLIPlugin(ABC):  # pragma no cover
    """Base class for a CLI submission plugin

    A plugin should derive from this class and implement one or more
    base methods (described below)

    Attributes:
        prog (str): command-line subcommand for which the plugin is active,
            e.g. "submit", "run", "alloc", "batch", "bulksubmit"
    """

    def __init__(self, prog, version=None, prefix="--ex-"):
        self.prog = prog
        if prog.startswith("flux "):
            self.prog = prog[5:]

    def add_options(self, parser):
        """Allow plugin to add additional options to the current command

        Plugins can simply use:
        >>> parser.add_argument("--longopt", action="store_true", help="Help.")

        to add new options to the current command. If the option should only
        be active for certain subcommands, then the option should be added
        conditionally based on :py:attr:`self.prog`.

        Args:
            parser: The :py:class:`Argparse` parser group for plugins, as
                created by :py:meth:`Argparse.ArgumentParser.add_argument_group`
        """
        pass

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

    def __init__(self):
        self.plugins = []
        etc = conf_builtin_get("confdir")
        if getenv("FLUX_CLI_PLUGINPATH"):
            self.plugindir = getenv("FLUX_CLI_PLUGINPATH")
        else:
            if etc is None:
                raise ValueError("failed to get builting confdir")
            self.plugindir = f"{etc}/cli/plugins"

    def _add_plugins(self, module, program):
        entries = [
            getattr(module, attr) for attr in dir(module) if not attr.startswith("_")
        ]
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

    def add_options(self, parser):
        """Call all plugin ``add_option`` callbacks for a arparser parser"""
        group = CLIPluginArgumentGroup(parser)
        for plugin in self.plugins:
            plugin.add_options(group)

    def preinit(self, args):
        """Call all plugin ``preinit`` callbacks"""
        for plugin in self.plugins:
            plugin.preinit(args)

    def modify_jobspec(self, args, jobspec):
        """Call all plugin ``modify_jobspec`` callbacks"""
        for plugin in self.plugins:
            plugin.modify_jobspec(args, jobspec)

    def validate(self, jobspec):
        """Call any plugin validate callback"""
        for plugin in self.plugins:
            plugin.validate(jobspec)
