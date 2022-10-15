###############################################################
# Copyright 2022 Lawrence Livermore National Security, LLC
# (c.f. AUTHORS, NOTICE.LLNS, COPYING)
#
# This file is part of the Flux resource manager framework.
# For details, see https://github.com/flux-framework.
#
# SPDX-License-Identifier: LGPL-3.0
###############################################################

import argparse
import os
from abc import abstractmethod

import flux
from flux.importer import import_path, import_plugins
from flux.job import Jobspec


class FrobnicatorPlugin:
    """Base class for plugins which modify jobspec in place"""

    def __init__(self, parser):
        """Initialize a FrobnicatorPlugin"""

    def configure(self, args, config):
        """Configure a FrobnicatorPlugin. Run after arguments are parsed

        Args:
           args (:obj:`Namespace`): The resulting namespace after calling
           argparse.parse_args()

           config (:obj:`dict`): The current broker config, stored as a Python
           dictionary.
        """

    @abstractmethod
    def frob(self, jobspec, userid, urgency, flags):
        """Modify jobspec. A FrobnicatorPlugin must implement this method.

        The plugin should modify the jobspec parameter directly. Extra
        job information (user, urgency, flags) are available in the
        ``info`` parameter.

        Args:
            jobspec (:obj:`Jobspec`): The jobspec to modify

            userid (:obj:`int`): Submitting user

            urgency (:obj:`int`): Initial job urgency

            flags (:obj:`int`): Job submission flags

        Returns:
            None or raises exception.
        """
        raise NotImplementedError


# pylint: disable=too-many-instance-attributes
class JobFrobnicator:
    """A plugin-based job modification class

    JobFrobnicator loads an ordered stack of plugins that implement the
    FrobnicatorPlugin interface from the 'flux.job.frobnicator.plugins'
    namespace.
    """

    default_frobnicators = ["defaults", "constraints"]
    plugin_namespace = "flux.job.frobnicator.plugins"

    def __init__(self, argv, pluginpath=None, parser=None):

        self.frobnicators = []
        self.config = {}

        if pluginpath is None:
            pluginpath = []

        if parser is None:
            parser = argparse.ArgumentParser(
                formatter_class=flux.util.help_formatter(), add_help=False
            )

        self.parser = parser
        self.parser_group = self.parser.add_argument_group("Options")
        self.plugins_group = self.parser.add_argument_group(
            "Options provided by plugins"
        )

        self.parser_group.add_argument("--plugins", action="append", default=[])

        args, self.remaining_args = self.parser.parse_known_args(argv)
        if not args.plugins:
            args.plugins = self.default_frobnicators
        else:
            args.plugins = [x for xs in args.plugins for x in xs.split(",")]

        #  Load all available frobnicator plugins
        self.plugins = import_plugins(self.plugin_namespace, pluginpath)
        self.args = args

    def start(self):
        """Read broker config, select and configure frobnicator plugins"""

        self.config = flux.Flux().rpc("config.get").get()

        for name in self.args.plugins:
            if name not in self.plugins:
                try:
                    self.plugins[name] = import_path(name)
                except:
                    raise ValueError(f"frobnicator plugin '{name}' not found")
            plugin = self.plugins[name].Frobnicator(parser=self.plugins_group)
            self.frobnicators.append(plugin)

        # Parse remaining args and pass result to loaded plugins
        args = self.parser.parse_args(self.remaining_args)
        for frobnicator in self.frobnicators:
            frobnicator.configure(args, config=self.config)

    def frob(self, jobspec, user=None, flags=None, urgency=16):
        """Modify jobspec using stack of loaded frobnicator plugins

        Args:
            jobspec (:obj:`Jobspec`): A Jobspec or JobspecV1 object
                    which will be modified in place

            userid (:obj:`int`): Submitting user

            flags (:obj:`int`): Job submission flags

            urgency (:obj:`int`): Initial job urgency

        Returns:
            :obj:`dict`: A dictionary containing a result object,
            including keys::

                {
                    'errnum': 0,
                    'errmsg': "An error message",
                    'data': jobspec or None
                }

        """
        if not isinstance(jobspec, Jobspec):
            raise ValueError("jobspec not an instance of Jobspec")

        if user is None:
            user = os.getuid()

        for frob in self.frobnicators:
            frob.frob(jobspec, user, flags, urgency)
        return {"errnum": 0, "data": jobspec}
